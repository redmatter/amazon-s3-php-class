/*
 * sms_inspector.hpp
 *
 *  Created on: 25 Jan 2011
 *	  Author: dkorah
 */

#ifndef SMS_INSPECTOR_HPP_
#define SMS_INSPECTOR_HPP_

#include "../common/globals.h"
#include "../common/database.hpp"

#include "../common/types.hpp"

#include <rml/utils.h>

#include <pugixml/pugixml.hpp>

#include <boost/noncopyable.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

namespace app
{
	std::string remove_hyphens(const std::string &str)
	{
		std::string buff(str);
		size_t hy;
		while ((hy = buff.find("-")) != std::string::npos)
			buff.erase(hy, 1);
		return buff;
	}

	std::string remove_redmatter(const std::string &str)
	{
		std::string buff(str);
		size_t rm;
		if ((rm = buff.find(".redmatter.com")) != std::string::npos)
			buff.erase(rm);
		return buff;
	}

	class sms_inspector: public boost::noncopyable
	{
		archiving::archivable_channel_t& in_channel;
		archiving::archivable_channel_t& out_channel;

		archiving::archivable_busy_set_t& archivable_busy_set_;

		database::internal& internal_db_;

		const boost::program_options::variables_map& confMap;

		const std::string& pbx_hostname_;

		app::coreapi& coreapi_;
		app::policy::cached_finder_t policy_cache_;

		boost::asio::io_service &io_service_;
		boost::thread_group threads_;
		std::vector<std::string> log_queue_;
		boost::mutex log_queue_mutex_;
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
			optsDesc.add_options()("sms_handler.number_of_threads",
					boost::program_options::value<unsigned int>()->default_value(2),
					"number of sms inspector threads");
		}

		/**
		 * Constructor
		 *
		 * @param in_channel
		 * @param out_channel
		 * @param confMap
		 * @return
		 */
		sms_inspector(archiving::archivable_channel_t& in_channel,
				archiving::archivable_channel_t& out_channel,
				archiving::archivable_busy_set_t& archivable_busy_set,
				database::internal& internal_db,
				const boost::program_options::variables_map& confMap,
				const std::string& pbx_hostname,
				app::coreapi& coreapi,
				boost::asio::io_service &io_service) :
				in_channel(in_channel), out_channel(out_channel), archivable_busy_set_(archivable_busy_set), internal_db_(
						internal_db), confMap(confMap), pbx_hostname_(pbx_hostname), coreapi_(coreapi),
						policy_cache_(boost::bind(&app::policy::finder, coreapi_, _1), "PolicyCache"),
						io_service_(io_service), threads_(), log_queue_(), log_queue_mutex_(), timer_(io_service_),
						first_run_complete_(false)
		{
		}

		void start(bool wait_for_first_run)
		{
			threads_.create_thread(boost::bind(&app::sms_inspector::smslog, this));

			while(wait_for_first_run && !first_run_complete_)
				boost::this_thread::sleep(boost::posix_time::seconds(1));
		}

		void smslog()
		{
			LogCritical("sms_inspector: smslog: thread starting");
			while (!app::quit)
			{
				try
				{
					// capture the current state of the log queue
					log_queue_mutex_.lock();
					std::vector<std::string> tmpvec = log_queue_;
					log_queue_.clear();
					log_queue_mutex_.unlock();

					if (!tmpvec.empty())
					{
						std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Messages>\n";
						for (std::vector<std::string>::iterator i = tmpvec.begin(); i != tmpvec.end(); ++i)
							xml += *i + "\n";
						xml += "</Messages>";

						app::coreapi::response r = coreapi_.post(std::string("/smslogs/push"), xml);
						if (r.first < 200 || r.first >= 300)
						{
							LogError("sms_inspector: smslog: error submitting SMS logs: " << r.first << ": " << r.second);

							log_queue_mutex_.lock();
							log_queue_.insert(log_queue_.begin(), tmpvec.begin(), tmpvec.end());
							log_queue_mutex_.unlock();
						}
						else
							LogMessage("sms_inspector: smslog: successfully submitted " << tmpvec.size() << " SMS records to the log");
					}

					timer_.expires_from_now(boost::posix_time::seconds(10));
					timer_.wait();
				}
				catch (boost::thread_interrupted)
				{
					LogCritical("sms_inspector: smslog: boost::thread_interrupted");
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
			}
			LogCritical("sms_inspector: smslog: thread stopping");
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
					archiving::archivable::pointer archivable_sms =	in_channel.consume();

					LogVerbose("sms_inspector::run - got from channel "	<< *archivable_sms);

					if (archivable_sms->get_current_state() >= archiving::archivable::state::inspected)
					{
						LogVerbose(*archivable_sms << " Already inspected; shifting to out channel");
						pc_produce(out_channel, archivable_sms);
					}
					else
					{
						// pull XML metadata out of SMS and add it to archivable

						pugi::xml_document doc;

						rml::utils::tmp_fstream::pointer ifs = archivable_sms->get_fstream_ptr();
						ifs->re_open();

						pugi::xml_parse_result res = doc.load(*ifs);
						if (!res)
							THROW_RML_EXCEPTION("Error parsing SMS; error:" << res.description());

						archivable_sms->hibernate();

						// start epoch

						time_t start_epoch = 0;
						pugi::xpath_node start_epoch_node = doc.select_single_node("/SMS/StartEpoch");
						if(start_epoch_node)
						{
							start_epoch = rml::utils::strtoull(start_epoch_node.node().child_value());
						}
						else
						{
							LogWarning("Start Epoch not found /SMS/StartEpoch; falling-back to current time");
							time(&start_epoch);
						}

						// OrgID/UserID

						archiving::owner_info::pointer owners;
						const char* rmOrgID_str = 0;
						archiving::org_id_t rmOrgID = 0;
						pugi::xpath_node rmOrgID_node =	doc.select_single_node("/SMS/OrgID");

						if (rmOrgID_node)
						{
							rmOrgID = (archiving::org_id_t) rml::utils::strtoull((rmOrgID_str = rmOrgID_node.node().child_value()), 0);
							LogVerbose("sms:" << archivable_sms->get_uuid() << " - rmOrgID: " << rmOrgID);
						}
						else
						{
							LogWarning("sms:" << archivable_sms->get_uuid() << " - No rmOrgID found");
							archivable_sms->set_done(true);

							if (!internal_db_.save(archivable_sms))
								LogWarning(*archivable_sms << " Save states to DB failed; queueing for retry");
							else
								archivable_busy_set_.remove(archivable_sms);

							continue;
						}

						archiving::user_id_t rmUserID = 0;
						pugi::xpath_node rmUserID_node = doc.select_single_node("/SMS/UserID");
						if (rmUserID_node)
						{
							rmUserID = (archiving::user_id_t) rml::utils::strtoll(rmUserID_node.node().child_value(), 0);
							LogVerbose("sms:" << archivable_sms->get_uuid() << " - rmUserID: " << rmUserID);
						}

						owners.reset(new archiving::owner_info(rmOrgID));
						owners->addUser(rmUserID);

						// hostname

						std::string hostname;
						pugi::xpath_node hostname_node = doc.select_single_node("/SMS/HostName");
						if (hostname_node)
						{
							hostname = hostname_node.node().child_value();
							LogVerbose("sms:" << archivable_sms->get_uuid() << " - HostName: " << hostname);
						}

						// policy id

						app::policy::id_type looked_up_policy_id;

						try
						{
							app::policy::pointer thepolicy = policy_cache_(std::make_pair(rmOrgID, app::policy::id_type(app::policy::pseudo_id::sms)));
							if (!thepolicy)
							{
								policy_cache_.forget(std::make_pair(rmOrgID, app::policy::id_type(app::policy::pseudo_id::sms)));
								throw rml::exception("error trying to get the policy");
							}

							looked_up_policy_id = thepolicy->id;
						}
						catch(rml::exception &e)
						{
							LogWarning("sms:" << archivable_sms->get_uuid() << " - policy cache lookup (" << rmOrgID << ",-2) threw an exception: " << e.what() << ". queuing for retry.");
							continue;
						}

						// commit the new data to the archivable object

						archivable_sms->set_owners(*owners); // we change this when we send the PUT request @see archivable::shove_put_request()
						archivable_sms->set_originator_uuid(archivable_sms->get_uuid());
						archivable_sms->set_start_epoch(start_epoch);
						if (!hostname.empty())
							archivable_sms->set_hostname(hostname);
						archivable_sms->set_policy_id(looked_up_policy_id);
						archivable_sms->set_current_state(archiving::archivable::state::inspected);

						if (!internal_db_.save(archivable_sms))
						{
							LogWarning(*archivable_sms << " Save states to DB failed; checking if we already processed it ...");

							try
							{
								archiving::archivable::pointer another_sms = internal_db_.load_archivable_by_uuid(archivable_sms->get_uuid());
								archivable_sms.swap(another_sms);

								if (!first_run_complete_)
								{
									if (archivable_sms->get_current_state() == archiving::archivable::state::stored ||
										archivable_sms->get_current_state() == archiving::archivable::state::discarded)
									{
										LogWarning(*archivable_sms << ": already stored/discarded: Trying to delete it");
										archivable_sms->set_done(archivable_sms->get_current_state() == archiving::archivable::state::discarded);
									}
									else if (archivable_sms->get_current_state() == archiving::archivable::state::inspected)
									{
										LogWarning(*archivable_sms << ": already inspected: sending it to the archive");
										pc_produce(out_channel, archivable_sms);
									}
									else
									{
										LogWarning(*archivable_sms << ": already collected/unknown: re-queueing it for initial processing");
										pc_produce(in_channel, archivable_sms);
									}
								}
								else
									LogWarning(*archivable_sms << ": state " << archivable_sms->get_current_state() << ": re-queueing it for initial processing");
							}
							catch(rml::exception &e)
							{
								LogWarning("sms:" << archivable_sms->get_uuid() << " threw an exception: " << e.what() << "so it doesn't exist, or there was a DB error, so we'll queue for retry");
							}

							continue;
						}

						pc_produce(out_channel, archivable_sms);

						std::string FromNumber(doc.select_single_node("/SMS/FromNumber").node().child_value());
						std::string ToNumber(doc.select_single_node("/SMS/ToNumber").node().child_value());

						std::string xml = boost::str (boost::format ("<SMS UUID=\"%1%\" OrgID=\"%2%\" UserID=\"%3%\" Time=\"%4%\" From_Number=\"%5%\" To_Number=\"%6%\" Direction=\"%7%\" HostName=\"%8%\"/>")
							% app::remove_hyphens(archivable_sms->get_uuid())
							% rmOrgID
							% rmUserID
							% start_epoch
							% (FromNumber.empty() ? "0" : FromNumber)
							% (ToNumber.empty() ? "0" : ToNumber)
							% (doc.select_single_node("/SMS/Direction").node().child_value() == "Inbound" ? "Inbound" : "Outbound")
							% app::remove_redmatter(hostname)
						);

						LogVerbose("sms: logging xml: " << xml);

						log_queue_mutex_.lock();
						log_queue_.push_back(xml);
						log_queue_mutex_.unlock();
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

#endif /* SMS_INSPECTOR_HPP_ */
