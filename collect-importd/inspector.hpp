/*
 * inspector.hpp
 *
 *  Created on: 25 Jan 2011
 *	  Author: dkorah
 */

#ifndef INSPECTOR_HPP_
#define INSPECTOR_HPP_

#include "../common/globals.h"
#include "../common/database.hpp"

#include "../common/types.hpp"

#include <rml/utils.h>

#include <pugixml/pugixml.hpp>

#include <boost/noncopyable.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/regex.hpp>

namespace app
{
	class inspector: public boost::noncopyable
	{
		archiving::archivable_channel_t& in_channel;
		archiving::archivable_channel_t& out_channel;

		archiving::archivable_busy_set_t& archivable_busy_set_;

		database::internal& internal_db_;

		const boost::program_options::variables_map& confMap;

		rml::coreapi& coreapi_;
		app::policy::cached_finder_t policy_cache_;

		boost::asio::io_service &io_service_;
		boost::asio::deadline_timer timer_;

		bool first_run_complete_;

	public:
		/**
		 * Describe confog
		 *
		 * @param optsDesc
		 */
		static void describe_config(
				boost::program_options::options_description& optsDesc)
		{
			optsDesc.add_options()("item_handler.number_of_threads",
					boost::program_options::value<unsigned int>()->default_value(2),
					"number of inspector threads");
		}

		/**
		 * Constructor
		 *
		 * @param in_channel
		 * @param out_channel
		 * @param confMap
		 * @return
		 */
		inspector(archiving::archivable_channel_t& in_channel,
				archiving::archivable_channel_t& out_channel,
				archiving::archivable_busy_set_t& archivable_busy_set,
				database::internal& internal_db,
				const boost::program_options::variables_map& confMap,
				const std::string& local_hostname,
				rml::coreapi& coreapi,
				boost::asio::io_service &io_service) :
				in_channel(in_channel), out_channel(out_channel), archivable_busy_set_(archivable_busy_set), internal_db_(
						internal_db), confMap(confMap), coreapi_(coreapi),
						policy_cache_(boost::bind(&app::policy::finder, coreapi_, _1), "PolicyCache"),
						io_service_(io_service), timer_(io_service_),
						first_run_complete_(false)
		{
		}

		void start(bool wait_for_first_run)
		{
			while(wait_for_first_run && !first_run_complete_)
				boost::this_thread::sleep(boost::posix_time::seconds(1));
		}

		/**
		 * run()
		 */
		void run()
		{
			while (!app::quit)
			{
				try
				{
					archiving::archivable::pointer archivable_item =	in_channel.consume();

					LogVerbose("inspector::run - got from channel "	<< *archivable_item);

					if (archivable_item->get_current_state() >= archiving::archivable::state::inspected)
					{
						LogVerbose(*archivable_item << " Already inspected; shifting to out channel");
						pc_produce(out_channel, archivable_item, 1);
					}
					else
					{
						// pull metadata out of file name and add it to archivable

						// regex to pull metadata out of file name
						// [1]: uuid (without dashes)
						// [2]: OrgID
						// [3]: UserID
						// [4]: Epoch
						// [5]: Provider (we place in 'hostname')
						static boost::regex metadata_regex("\\/([0-9a-f\\-]{36})_([0-9]+)_([0-9]+)_([0-9]+)_(.+)\\.mp3$");

						std::string file_path = archivable_item->get_paths().first;
						boost::smatch match;
						if (!boost::regex_search(file_path, match, metadata_regex) || match.size() != 6) {
							archivable_item->hibernate();
							LogError(std::string("Filename was not in expected format: ") + file_path);
                            continue;
						}

						// start epoch

						time_t start_epoch = rml::utils::strtoull(match[4].str().c_str());

						// OrgID/UserID

						archiving::owner_info::pointer owners;
						archiving::org_id_t rmOrgID = 0;
						archiving::user_id_t rmUserID = 0;

						rmOrgID = (archiving::org_id_t) rml::utils::strtoull(match[2].str().c_str(), 0);
						LogVerbose("import:" << archivable_item->get_uuid() << " - rmOrgID: " << rmOrgID);

						rmUserID = (archiving::user_id_t) rml::utils::strtoll(match[3].str().c_str(), 0);
						LogVerbose("import:" << archivable_item->get_uuid() << " - rmUserID: " << rmUserID);

						owners.reset(new archiving::owner_info(rmOrgID));
						owners->addUser(rmUserID);

						// hostname

						std::string hostname = match[5].str();
						LogVerbose("import:" << archivable_item->get_uuid() << " - HostName: " << hostname);

						// policy id

						app::policy::id_type looked_up_policy_id;

						try
						{
							app::policy::pointer thepolicy = policy_cache_(std::make_pair(rmOrgID, app::policy::id_type(app::policy::pseudo_id::recording)));
							if (!thepolicy)
							{
								policy_cache_.forget(std::make_pair(rmOrgID, app::policy::id_type(app::policy::pseudo_id::recording)));
								throw rml::exception("error trying to get the policy");
							}

							looked_up_policy_id = thepolicy->id;
						}
						catch(rml::exception &e)
						{
							LogWarning("import:" << archivable_item->get_uuid() << " - policy cache lookup (" << rmOrgID << ",-2) threw an exception: " << e.what() << ". queuing for retry.");
							continue;
						}

						// commit the new data to the archivable object

						archivable_item->set_owners(*owners); // we change this when we send the PUT request @see archivable::inject_put_request()
						archivable_item->set_originator_uuid(archivable_item->get_uuid());
						archivable_item->set_start_epoch(start_epoch);
						archivable_item->set_hostname(hostname);
						archivable_item->set_policy_id(looked_up_policy_id);
						archivable_item->set_current_state(archiving::archivable::state::inspected);

						if (!internal_db_.save(archivable_item))
						{
							LogWarning(*archivable_item << " Save states to DB failed; checking if we already processed it ...");

							try
							{
								archiving::archivable::pointer another_item = internal_db_.load_archivable_by_uuid(archivable_item->get_uuid());
								archivable_item.swap(another_item);

								if (!first_run_complete_)
								{
									if (archivable_item->get_current_state() == archiving::archivable::state::stored ||
										archivable_item->get_current_state() == archiving::archivable::state::discarded)
									{
										LogWarning(*archivable_item << ": already stored/discarded: Trying to delete it");
										archivable_item->set_done(archivable_item->get_current_state() == archiving::archivable::state::discarded);
									}
									else if (archivable_item->get_current_state() == archiving::archivable::state::inspected)
									{
										LogWarning(*archivable_item << ": already inspected: sending it to the archive");
										pc_produce(out_channel, archivable_item, 1);
									}
									else
									{
										LogWarning(*archivable_item << ": already collected/unknown: re-queueing it for initial processing");
										pc_produce(in_channel, archivable_item);
									}
								}
								else
									LogWarning(*archivable_item << ": state " << archivable_item->get_current_state() << ": re-queueing it for initial processing");
							}
							catch(rml::exception &e)
							{
								LogWarning("import: " << archivable_item->get_uuid() << " threw an exception: " << e.what() << "so it doesn't exist, or there was a DB error, so we'll queue for retry");
							}

							continue;
						}

						pc_produce(out_channel, archivable_item, 1);
					}
				}
				catch (const archiving::archivable_channel_t::finishing_exception& e)
				{
					LogVerbose(e.what());
					break;
				}
				catch (const std::exception& e)
				{
					LogWarning("exception:" << e.what());
				}
				catch (...)
				{
					LogCritical("unexpected exception");
				}

				first_run_complete_ = true;
			}
			LogMessage("finishing");
		}
	};
} // namespace app

#endif /* INSPECTOR_HPP_ */
