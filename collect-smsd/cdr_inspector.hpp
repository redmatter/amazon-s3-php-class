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

namespace
{
	inline const char* get_xpath_single_value(pugi::xml_node& doc,
			const std::string& xpath, const char* default_value)
	{
		RML_DEBUG(LogDebug("xpath: " << xpath << " default_value:" << default_value));

		pugi::xpath_node xpath_node = doc.select_single_node(xpath.c_str());

		if (xpath_node)
		{
			RML_DEBUG(LogDebug(xpath << '=' << xpath_node.node().value()));
			return xpath_node.node().value();
		}

		RML_DEBUG(
				if (default_value)
				LogDebug(xpath<<'='<<default_value);
				else
				LogDebug(xpath<<"=NULL");
		);

		return default_value;
	}

	inline std::string get_xpath_single_value(pugi::xml_node& doc,
			const std::string& xpath, const std::string& default_value = "")
	{
		RML_DEBUG(LogDebug("xpath: " << xpath << " default_value:" << default_value));
		return get_xpath_single_value(doc, xpath, default_value.c_str());
	}

	inline unsigned long long get_xpath_single_value(pugi::xml_node& doc,
			const std::string& xpath,
			const unsigned long long& default_value = 0ULL)
	{
		RML_DEBUG(LogDebug("xpath: " << xpath << " default_value:" << default_value));
		return rml::utils::strtoull(
				get_xpath_single_value(doc, xpath, (const char*) 0), default_value);
	}

	inline long long get_xpath_single_value(pugi::xml_node& doc,
			const std::string& xpath, const long long default_value = 0LL)
	{
		RML_DEBUG(LogDebug("xpath: " << xpath << " default_value:" << default_value));
		return rml::utils::strtoll(
				get_xpath_single_value(doc, xpath, (const char*) 0), default_value);
	}
}

namespace app
{
	class sms_inspector: public boost::noncopyable
	{
		archiving::archivable_channel_t& in_channel;
		archiving::archivable_channel_t& out_channel;

		database::internal& internal_db_;

		const boost::program_options::variables_map& confMap;

		const std::string& pbx_hostname_;

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
				database::internal& internal_db,
				const boost::program_options::variables_map& confMap,
				const std::string& pbx_hostname) :
				in_channel(in_channel), out_channel(out_channel), internal_db_(
						internal_db), confMap(confMap), pbx_hostname_(pbx_hostname)
		{
		}

		/**
		 * run()
		 */
		void run()
		{
			while (!app::quit)
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
							out_channel.produce(archivable_sms);
						}
						else
						{
							// pull XML metadata out of SMS and add it to archivable

							rml::utils::tmp_fstream::pointer ifs = archivable_sms->get_fstream_ptr();
							ifs->re_open();

							pugi::xml_document doc;

							{
								pugi::xml_parse_result res = doc.load(*ifs);
								if (!res)
									THROW_RML_EXCEPTION("Error parsing SMS; error:" << res.description());
							}

							// start epoch

							time_t start_epoch = 0;
							pugi::xpath_node start_epoch_node = doc.select_single_node("StartEpoch/text()");
							if(start_epoch_node)
							{
								start_epoch = rml::utils::strtoull(start_epoch_node.node().value());
							}
							else
							{
								LogWarning("Start Epoch not found ( /sms/start_epoch ); falling-back to current time");
								time(&start_epoch);
							}

							// OrgID/UserID

							archiving::owner_info::pointer owners;
							const char* rmOrgID_str = 0;
							archiving::org_id_t rmOrgID = 0;
							pugi::xpath_node rmOrgID_node =	doc.select_single_node("OrgID/text()");

							if (rmOrgID_node)
							{
								rmOrgID = (archiving::org_id_t) rml::utils::strtoull((rmOrgID_str = rmOrgID_node.node().value()), 0);
								LogVerbose("sms:" << archivable_sms->get_uuid() << " - rmOrgID: " << rmOrgID);
							}
							else
							{
								LogCritical("sms:" << archivable_sms->get_uuid() << " - No rmOrgID found");
								archivable_sms->set_done(true);
								archivable_sms.reset();
								continue;
							}

							archiving::user_id_t rmUserID = 0;
							pugi::xpath_node rmUserID_node = doc.select_single_node("UserID/text()");
							if (rmUserID_node)
							{
								rmUserID = (archiving::user_id_t) rml::utils::strtoll(rmUserID_node.node().value(), 0);
								LogVerbose("sms:" << archivable_sms->get_uuid() << " - rmUserID: " << rmUserID);
							}

							owners.reset(new archiving::owner_info(rmOrgID));
							owners->addUser(rmUserID);

							// hostname

							std::string hostname;
							pugi::xpath_node hostname_node = doc.select_single_node("HostName/text()");
							if (hostname_node)
							{
								hostname = hostname_node.node().value();
								LogVerbose("sms:" << archivable_sms->get_uuid() << " - HostName: " << hostname);
							}

							// commit the new data to the archivable object

							archivable_sms->set_owners(*owners); // we change this when we send the PUT request @see archivable::shove_put_request()
							archivable_sms->set_originator_uuid(archivable_sms->get_uuid());
							archivable_sms->set_start_epoch(start_epoch);
							if (!hostname.empty())
								archivable_sms->set_hostname(hostname);
							archivable_sms->set_current_state(archiving::archivable::state::inspected);

							int retry = 0;
							while (!internal_db_.save(archivable_sms) && !app::quit)
							{
								LogMessage(*archivable_sms << " [retry: " << retry << "] Save states to DB failed; retrying in 15 seconds ...");
								boost::this_thread::sleep(boost::posix_time::seconds(15));
								retry++;
							}

							out_channel.produce(archivable_sms);
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
				}
			}
			LogMessage("finishing");
		}
	};
} // namespace app

#endif /* SMS_INSPECTOR_HPP_ */
