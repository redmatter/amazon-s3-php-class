/*
 * cdr_inspector.hpp
 *
 *  Created on: 25 Jan 2011
 *	  Author: dkorah
 */

#ifndef CDR_INSPECTOR_HPP_
#define CDR_INSPECTOR_HPP_

#include "globals.h"
#include "database.hpp"

#include "../common/types.hpp"

#include <rml/utils.h>

#include <pugixml/pugixml.hpp>

#include <boost/noncopyable.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

namespace {

inline const char* get_xpath_single_value(pugi::xml_node& doc, const std::string& xpath, const char* default_value)
{
	RML_DEBUG( LogDebug("xpath: "<<xpath<<" default_value:"<<default_value) );
	pugi::xpath_node xpath_node = doc.select_single_node(xpath.c_str());
	if(xpath_node) {
		RML_DEBUG(LogDebug(xpath<<'='<<xpath_node.node().value()));
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

inline std::string get_xpath_single_value(pugi::xml_node& doc, const std::string& xpath, const std::string& default_value = "")
{
	RML_DEBUG(LogDebug("xpath: "<<xpath<<" default_value:"<<default_value));
	return get_xpath_single_value(doc, xpath, default_value.c_str());
}

inline unsigned long long get_xpath_single_value(pugi::xml_node& doc, const std::string& xpath, const unsigned long long& default_value = 0ULL)
{
	RML_DEBUG(LogDebug("xpath: "<<xpath<<" default_value:"<<default_value));
	return rml::utils::strtoull(get_xpath_single_value(doc, xpath, (const char*)0), default_value);
}

inline long long get_xpath_single_value(pugi::xml_node& doc, const std::string& xpath, const long long default_value = 0LL)
{
	RML_DEBUG(LogDebug("xpath: "<<xpath<<" default_value:"<<default_value));
	return rml::utils::strtoll(get_xpath_single_value(doc, xpath, (const char*)0), default_value);
}

}

namespace app {

class cdr_inspector : public boost::noncopyable
{

	archiving::archivable_channel_t& in_channel;
	archiving::archivable_channel_t& out_channel;

	database::internal& internal_db_;

	const boost::program_options::variables_map& confMap;

	std::vector<archiving::org_id_t> orgs_enabled_;

	const std::string& pbx_hostname_;

	struct leg
	{
		enum type_t
		{
			unknown = 0,
			A, B
		};
	};

public:
	/**
	 * Describe confog
	 *
	 * @param optsDesc
	 */
	static void describe_config(boost::program_options::options_description& optsDesc) {
		optsDesc.add_options()
				("cdr_handler.filter_orgs", boost::program_options::value<std::string>()->default_value(""), "OrgIDs for which filter is turned on")
				("cdr_handler.honour_cdr_variable_rmCDRDiscard", boost::program_options::value<std::string>()->default_value("false"), "honour variable rmCDRDiscard in CDRs")
				("cdr_handler.filtered_xpaths", boost::program_options::value<std::string>()->default_value(""), "filtered xpaths in CDRs")
				("cdr_handler.number_of_threads", boost::program_options::value<unsigned int>()->default_value(2), "number of cdr inspector threads")
				("cdr_handler.archiving_enabled_orgs", boost::program_options::value<std::string>()->default_value("1"), "archiving enabled orgs; for phased rollout")

				("temp_files.base_directory", boost::program_options::value<std::string>()->default_value(std::string("/var/lib/archiving/collectd")), "collectd temp data directory")
				;
	}

	/**
	 * Constructor
	 *
	 * @param in_channel
	 * @param out_channel
	 * @param confMap
	 * @return
	 */
	cdr_inspector(archiving::archivable_channel_t& in_channel, archiving::archivable_channel_t& out_channel, database::internal& internal_db, const boost::program_options::variables_map& confMap, const std::string& pbx_hostname) :
		in_channel(in_channel),
		out_channel(out_channel),
		internal_db_(internal_db),
		confMap(confMap),
		pbx_hostname_(pbx_hostname)
	{
		if(confMap.count("cdr_handler.archiving_enabled_orgs")) {
			const std::string& orgs_enabled(confMap["cdr_handler.archiving_enabled_orgs"].as<std::string>());
			if(!orgs_enabled.empty()) {
				std::vector<std::string> orgs_enabled_str;
				boost::algorithm::split(orgs_enabled_str, orgs_enabled, boost::is_any_of(", \t"));
				std::vector<std::string>::const_iterator end = orgs_enabled_str.end();
				for(std::vector<std::string>::const_iterator itr = orgs_enabled_str.begin(); itr != end; ++itr) {
					LogVerbose("cdr_handler.archiving_enabled_orgs: "<<*itr);
					orgs_enabled_.push_back(rml::utils::strtoull(itr->c_str()));
				}
			}
		}

	}

	/**
	 * run()
	 */
	void run()
	{
		while(!app::quit) {

			bool enable_rmCDRDiscard = false;
			std::vector<std::string> filter_orgs;
			std::vector<std::string> filtered_xpaths;
			std::string temp_files_base_directory;
			bool filter_orgs_all = false;

			// load OR reload (on HUP) config
			{
				enable_rmCDRDiscard = (confMap["cdr_handler.honour_cdr_variable_rmCDRDiscard"].as<std::string>() == "true");

				{
					filter_orgs.clear();
					const std::string& filter_orgs_str(confMap["cdr_handler.filter_orgs"].as<std::string>());
					if(!filter_orgs_str.empty()) {
						if(filter_orgs_str == "all")
							filter_orgs_all = true;
						else
							boost::split(filter_orgs, filter_orgs_str, boost::is_any_of(", \t"));
					}
				}

				{
					filtered_xpaths.clear();
					const std::string& filtered_xpaths_str(confMap["cdr_handler.filtered_xpaths"].as<std::string>());
					if(!filtered_xpaths_str.empty()) {
						boost::split(filtered_xpaths, filtered_xpaths_str, boost::is_any_of(", \t"));
					}
				}

				{
					temp_files_base_directory = confMap["temp_files.base_directory"].as<std::string>();
				}
			}

			while(!app::quit) {
				try {
					archiving::archivable::pointer archivable_cdr = in_channel.consume();
					LogVerbose("cdr_inspector::run - got from channel "<<*archivable_cdr);

					if((archivable_cdr->get_type() != archiving::archivable::file_type::cdr) || (archivable_cdr->get_current_state() >= archiving::archivable::state::inspected)) {
						LogVerbose(*archivable_cdr<<" Already inspected; shifting to out channel");
						out_channel.produce(archivable_cdr);
						continue;
					}


					// inspect and cleanup CDR
					{
						rml::utils::tmp_fstream::pointer ifs = archivable_cdr->get_fstream_ptr();
						ifs->re_open();

						bool modified = false;
						bool isDiscardable = false;
						leg::type_t leg;

						pugi::xml_document doc;
						pugi::xml_node variables_node;
						{
							pugi::xml_parse_result res = doc.load(*ifs);
							if(!res)
								THROW_RML_EXCEPTION("Unable to load CDR for filtering; error:"<<res.description());

							pugi::xpath_node variables_xpath_node;
							if(!(variables_xpath_node = doc.select_single_node("/cdr/variables")))
								throw rml::exception("/cdr/variables NOT found");

							variables_node = variables_xpath_node.node();
						}

//						fn_xpath_t xpath = boost::bind(get_xpath_single_value, boost::ref(doc), _1, _2);

						const char* cdr_uuid = variables_node.select_single_node("uuid/text()").node().value();
						LogDebug("cdr_uuid: "<<cdr_uuid);

						{
							pugi::xpath_node direction_node = doc.select_single_node("/cdr/channel_data/direction/text()");
							RML_DEBUG(
								if(!direction_node)
									LogVerbose("/cdr/channel_data/direction NOT found");
								else
									LogVerbose("/cdr/channel_data/direction = "<<direction_node.node().value());
							);

							pugi::xpath_node rmAssumeALeg_node = variables_node.select_single_node("rmAssumeALeg/text()");
							RML_DEBUG(
								if(!rmAssumeALeg_node)
									LogVerbose("/cdr/channel_data/rmAssumeALeg NOT found");
								else
									LogVerbose("/cdr/channel_data/rmAssumeALeg = "<<rmAssumeALeg_node.node().value());
							);

							leg = ((direction_node && (0 == strncasecmp("inbound", direction_node.node().value(), 7))) || (rmAssumeALeg_node && (0 == strncasecmp("true", rmAssumeALeg_node.node().value(), 4))))? leg::A : leg::B;
						}

						time_t start_epoch = 0;
						{
							pugi::xpath_node start_epoch_node = variables_node.select_single_node("start_epoch/text()");
							if(start_epoch_node) {
								start_epoch = rml::utils::strtoull(start_epoch_node.node().value());
							}
							else {
								LogWarning("Start Epoch not found ( /cdr/variables/start_epoch ); falling-back to current time");
								time(&start_epoch);
							}
						}

						const char* originator_uuid = 0;
						{
							if(leg == leg::A) {
								originator_uuid = cdr_uuid;
							}
							else {
								pugi::xpath_node OriginatorUUID_node = variables_node.select_single_node("rmOriginatorUUID/text()");
								if(OriginatorUUID_node) {
									originator_uuid = OriginatorUUID_node.node().value();
									LogVerbose("OriginatorUUID from /cdr/variables/rmOriginatorUUID "<<originator_uuid);
								}
								else if(pugi::xpath_node originator_node = variables_node.select_single_node("originator/text()")) {
									originator_uuid = originator_node.node().value();
									LogVerbose("OriginatorUUID from /cdr/variables/originator "<<originator_uuid);
								}
								else {
									originator_uuid = "";
									LogError("cdr:"<<cdr_uuid<<" - OriginatorUUID NOT found");
								}
							}
						}

						archiving::owner_info::pointer owners;
						const char* rmOrgID_str = 0;
						{
							archiving::org_id_t rmOrgID = 0;
							{
								pugi::xpath_node rmOrgID_node = variables_node.select_single_node(leg == leg::A? "rmOrgID/text()": "rmOriginatorOrgId/text()");
								if(rmOrgID_node) {
									rmOrgID = (archiving::org_id_t)rml::utils::strtoull(( rmOrgID_str = rmOrgID_node.node().value() ), 0);
									LogVerbose("cdr:"<<cdr_uuid<<" - rmOrgID: "<<rmOrgID);
								}
								else
									LogWarning("No rmOrgId/rmOriginatorOrgId found; using 0");
							}

							if(!rmOrgID) {
								LogCritical("cdr:"<<cdr_uuid<<" - No rmOrgID found");
								archivable_cdr->set_done(true);
								archivable_cdr.reset();
								continue;
							}

							archiving::user_id_t rmUserID = 0;
							{
								pugi::xpath_node rmUserID_node = variables_node.select_single_node("rmUserID/text()");
								if(rmUserID_node) {
									rmUserID = (archiving::user_id_t)rml::utils::strtoll(rmUserID_node.node().value(), 0);
									LogVerbose("cdr:"<<cdr_uuid<<" - rmUserID: "<<rmUserID);
								}
							}

							{
								owners.reset(new archiving::owner_info(rmOrgID));
								owners->addUser(rmUserID);
								archivable_cdr->set_owners(*owners); // we change this when we send the PUT request @see archivable::shove_put_request()
							}
						}

						if(filter_orgs_all || !filter_orgs.empty() || enable_rmCDRDiscard) {

							try {
								if(enable_rmCDRDiscard) {
									pugi::xpath_node rmCDRDiscard_node = variables_node.select_single_node("rmCDRDiscard/text()");
									if((isDiscardable = (0 == strncasecmp("true", rmCDRDiscard_node.node().value(), 4))))
										LogMessage("cdr:"<<cdr_uuid<<" - rmCDRDiscard=true marked discardable");
								}

								if(!isDiscardable) {
									if((leg == leg::B) && (filter_orgs_all || (filter_orgs.end() != std::find(filter_orgs.begin(), filter_orgs.end(), rmOrgID_str)))) {

										const bool& isLegAnswered(0 == strncasecmp("ANSWER", variables_node.select_single_node("endpoint_disposition/text()").node().value(), 6));
										LogVerbose("cdr:"<<cdr_uuid<<" - isLegAnswered:"<<std::iostream::boolalpha<<isLegAnswered);

										if(!isLegAnswered) {
											isDiscardable = true;
											LogMessage("cdr:"<<cdr_uuid<<" - Un-ANSWERed B leg marked discardable");
										}
									}
								}
							}
							catch(const std::exception& e) {
								LogError("exception: "<<e.what());
								throw;
							}
						}

						if( isDiscardable ) {
							archivable_cdr->set_done(true);
							archivable_cdr.reset();
							continue;
						}

						// still the ifs and doc are intact; lets inspect it before we modify it
						// saves another XML parsing!

						RML_DEBUG(
							LogDebug("owners->orgID: " << owners->orgID);
							LogDebug("orgs_enabled_.empty(): "<<RML_YES_NO(orgs_enabled_.empty()));
							LogDebug("orgs_enabled_.end() != std::find(orgs_enabled_.begin(), orgs_enabled_.end(), owners->orgID)): " << (orgs_enabled_.end() != std::find(orgs_enabled_.begin(), orgs_enabled_.end(), owners->orgID)));
						);

						if(orgs_enabled_.empty() || (orgs_enabled_.end() != std::find(orgs_enabled_.begin(), orgs_enabled_.end(), owners->orgID))) {

							// switch between the new variables and old
							// if rmCallRecordingCount set, we are in the new scheme
							pugi::xpath_node node;
							if((node = variables_node.select_single_node("rmCallRecordingCount/text()"))) {
								archiving::archivable::pointer archivable_recording;

								unsigned int recording_count( rml::utils::strtoull(node.node().value(), 0) );
								if(recording_count) {
									variables_node.append_child("rmArchivingEnabled").set_value("true");
									modified = true;
								}

								LogVerbose("cdr:"<<cdr_uuid<<" - rmCallRecordingCount: "<<recording_count);

								for(; recording_count; recording_count--) {

									const std::string& recording_location( get_xpath_single_value(variables_node, boost::str(boost::format("rmCallRecordingLoc_%1%/text()") % recording_count), std::string()));
									const app::policy::id_type& recording_policy( get_xpath_single_value(variables_node, boost::str(boost::format("rmCallRecordingPolicy_%1%/text()") % recording_count), 0LL) );

									const bool& recording_send_email( get_xpath_single_value(variables_node, boost::str(boost::format("rmCallRecordingEmail_%1%") % recording_count), std::string()) == "true" );
									const char* recording_send_email_to_recips = get_xpath_single_value(variables_node, boost::str(boost::format("rmCallRecordingToRecips_%1%/text()") % recording_count), (const char*)0);

									if(!recording_location.empty()) {
										try {
											std::string uuid(recording_location, recording_location.length() - (36/*uuid*/+4/*.wav*/), 36);
											archivable_recording.reset(new archiving::archivable(uuid, originator_uuid, start_epoch, recording_location, archiving::archivable::file_type::recording, "wav", pbx_hostname_, archivable_cdr, recording_policy));

											variables_node.append_child(boost::str(boost::format("rmCallRecordingUUID_%1%") % recording_count).c_str()).set_value(uuid.c_str());
											modified = true;

											archivable_recording->set_owners(*owners);

											LogMessage(*archivable_cdr<<" - Archivable recording found - "<<*archivable_recording);

											if(recording_policy < 0 || (recording_send_email && (0 < strlen(recording_send_email_to_recips))))
												archivable_recording->set_add_to_cdrmunch_cache(true);

											archivable_recording->set_current_state(archiving::archivable::state::inspected);
											{
												int retry = 0;
												while(!internal_db_.save(archivable_recording) && (retry < archiving::archivable::save_retry_max_count)) {
													LogMessage(*archivable_recording<<" [retry:"<<retry<<"] Save failed; retrying ... (max-retry: "<<archiving::archivable::save_retry_max_count<<')');
													boost::this_thread::sleep(boost::posix_time::seconds(1 + retry * archiving::archivable::save_retry_multiplier_seconds));
													retry++;
												}
											}


											out_channel.produce(archivable_recording);
										}
										catch(const std::exception& e) {
											LogWarning("Failed producing archivable item; error: "<<e.what());
											// add a flag to signal this

											variables_node.append_child(boost::str(boost::format("rmCallRecordingUnavailable_%1%") % recording_count).c_str()).set_value("true");
											modified = true;
										}
									}
								}
							}
							else if((node = doc.select_single_node("/cdr/variables[ rmDispRecordRetain = 'true' or rmDispPersonalRecordRetain = 'true' or rmDispRecordEmailSend = 'true' or rmDispPersonalRecordEmailSend = 'true' ]/rmLocalRecordLocation/text()"))) {
								try {
									archiving::archivable::pointer archivable_recording;

									LogMessage(*archivable_cdr<<" - Archivable recording found.");

									const app::policy::id_type recording_policy( doc.select_single_node("/cdr/variables[ rmDispRecordRetain = 'true' or rmDispPersonalRecordRetain = 'true' ]")? 0: -1);

									std::string file_path(node.node().value());
									std::string uuid(file_path, file_path.length() - (36/*uuid*/+4/*.wav*/), 36);
									archivable_recording.reset(new archiving::archivable(uuid, originator_uuid, start_epoch, file_path, archiving::archivable::file_type::recording, "wav", pbx_hostname_, archivable_cdr, recording_policy));

									variables_node.append_child("rmCallRecordingUUID").set_value(uuid.c_str());
									modified = true;

									{
										// should we add this to CDRMunch Cache ?
										const bool rmDispRecordEmailSend( "false" != get_xpath_single_value(variables_node, "rmDispRecordEmailSend/text()", std::string("false")) );
										const bool rmDispPersonalRecordEmailSend( "false" != get_xpath_single_value(variables_node, "rmDispPersonalRecordEmailSend/text()", std::string("false")) );
										const bool rmDispRecordToRecips(get_xpath_single_value(variables_node, "rmDispRecordToRecips/text()", std::string()).empty());
										const bool rmDispPersonalRecordToRecips(get_xpath_single_value(variables_node, "rmDispPersonalRecordToRecips/text()", std::string()).empty());

										if((rmDispRecordEmailSend && rmDispRecordToRecips) || (rmDispPersonalRecordEmailSend && rmDispPersonalRecordToRecips))
											archivable_recording->set_add_to_cdrmunch_cache(true);
										else if(recording_policy == -1)
											throw rml::exception("NO recipients specified and NOT requested to retain");
									}

									archivable_recording->set_owners(*owners);

									LogWarning(*archivable_cdr<<" - Archivable recording found via the OLD variables; Beware of CDRMunch incompatibility! file: "<<*archivable_recording);

									archivable_recording->set_current_state(archiving::archivable::state::inspected);
									{
										int retry = 0;
										while(!internal_db_.save(archivable_recording) && (retry < archiving::archivable::save_retry_max_count)) {
											LogMessage(archivable_recording<<" [retry:"<<retry<<"] Save failed; retrying ... (max-retry: "<<archiving::archivable::save_retry_max_count<<')');
											boost::this_thread::sleep(boost::posix_time::seconds(1 + retry * archiving::archivable::save_retry_multiplier_seconds));
											retry++;
										}
									}

									out_channel.produce(archivable_recording);
								}
								catch(const std::exception& e) {
									LogWarning("Failed producing archivable item; error: "<<e.what());

									variables_node.append_child("rmCallRecordingUnavailable").set_value("true");
									modified = true;
								}
							}
						}

						// We need to strip out any unwanted tags using filtered_xpaths[]
						if(!filtered_xpaths.empty()) {
							BOOST_FOREACH ( const std::string& xpath, filtered_xpaths ) {
								pugi::xpath_node_set nodes = doc.select_nodes(&xpath[0]);
								for(pugi::xpath_node_set::const_iterator it = nodes.begin(); it != nodes.end(); ++it) {
									(*it).node().parent().remove_child((*it).node());
									modified = true;
								}

								if(modified) {
									rml::utils::tmp_fstream::pointer tmp_fsptr(new rml::utils::tmp_fstream(archivable_cdr->get_fstream_ptr()->get_filename(true)+'.', 0U, temp_files_base_directory));
									doc.save(*tmp_fsptr, "  ");
									archivable_cdr->set_fstream(tmp_fsptr);
									LogVerbose(*archivable_cdr<<" modified");
								}
							}
						}

						archivable_cdr->set_originator_uuid(originator_uuid);
						archivable_cdr->set_start_epoch(start_epoch);
						archivable_cdr->set_add_to_cdrmunch_cache(true);

						archivable_cdr->set_current_state(archiving::archivable::state::inspected);
						{
							int retry = 0;
							while(!internal_db_.save(archivable_cdr) && (retry < archiving::archivable::save_retry_max_count)) {
								LogMessage(*archivable_cdr<<" [retry:"<<retry<<"] Save failed; retrying ... (max-retry: "<<archiving::archivable::save_retry_max_count<<')');
								boost::this_thread::sleep(boost::posix_time::seconds(1 + retry * archiving::archivable::save_retry_multiplier_seconds));
								retry++;
							}
						}

						out_channel.produce(archivable_cdr);
					}
				}
				catch(const archiving::archivable_channel_t::finishing_exception& e) {
					LogVerbose(e.what());
					break;
				}
				catch(const std::exception& e) {
					LogWarning("exception:"<<e.what());
				}
				catch(...) {
					LogCritical("unexpected exception");
				}
			}
		}
		LogMessage("finishing");
	}
};

}  // namespace app

#endif /* CDR_INSPECTOR_HPP_ */
