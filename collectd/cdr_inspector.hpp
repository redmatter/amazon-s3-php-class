//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 25/01/2011.
//

#ifndef CDR_INSPECTOR_HPP_
#define CDR_INSPECTOR_HPP_

#include "database_sqlite.hpp"
#include "../common/globals.h"
#include "../common/types.hpp"
#include "../common/archivable.hpp"
#include "../common/collectd/callbacks.hpp"
#include "archivable/catalogue.hpp"

#include <rml/utils.h>
#include <rml/queue/error.hpp>

#include <pugixml/pugixml.hpp>

#include <boost/noncopyable.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/regex.hpp>
#include <boost/make_shared.hpp>

#include <uuid/uuid.h>
#include <ctime>

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

// Checks to see if the passed archivable was in the passed directory and, if so, attempts to extract information
// from the filename and populate archivable with this information.
// Returns false if there was a failure.
bool check_info_in_filename(const std::string& directory, const archiving::archivable::pointer& p_archivable)
{
	// Create this regex as a static variable so that we don't construct it each time.
	// We're expecting the path to look like:
	// /various/directory/elements/OrgID_ArchivePolicyID_CallUUID_UUID.extension.or.more
	// Do a regular expression for the text between a starting / and an trailing .ext which is made up of numbers and
	// UUIDs, e.g. 123_456_01234567-abcd-8901-efab-234567890123_01234567-abcd-8901-efab-234567890123
	// The below is close enough to extract the string we need.
	static boost::regex info_regex("(?<=/)[0-9]+_[0-9]+_[a-zA-Z0-9-]+_[a-zA-Z0-9-]+(?=\\.[a-zA-Z]+)");

	// Check if the file was in the appropriate directory.
	std::string path(p_archivable->get_paths().first);
	if (path.find(directory) != 0)
	{
		LogVerbose("directory: " << directory << " not found in path: " << path);
		return true;
	}

	LogVerbose("Attempting to extract info from filename");

	// Check that the appropriate format exists in the filename.
	boost::smatch match;
	if (!boost::regex_search(path, match, info_regex) || match.size() != 1)
	{
		LogWarning("Filename was not in expected format: " << path);
		return false;
	}

	// Get the useful bit from the path.
	path = match[0].str();

	LogVerbose("Matched path: " << path);

	// Split the path using _ as the separator.
	std::vector<std::string> split_string;
	boost::split(split_string, path, boost::is_any_of("_"));

	// Based on the above format, we should expect that there will be four strings.
	if (split_string.size() != 4)
	{
		LogWarning("String is not in expected format: " << path);
		return false;
	}
	else
	{
		// The first string should be the OrgID, use this to create owner_info.
		p_archivable->set_owners(archiving::owner_info(rml::utils::strtoull(split_string[0].c_str())));

		// The second string should be ArchivePolicyID.
		p_archivable->set_policy_id(rml::utils::strtoull(split_string[1].c_str()));

		// The third string should be CallUUID.
		p_archivable->set_originator_uuid(split_string[2]);

		// The fourth string should be UUID, but we don't need to populate this.

		return true;
	}
}

}

namespace app {

class cdr_inspector : public boost::noncopyable
{
	const common::collectd::fn_consume_with_timeout_type &fn_in_queue_consume_;
	const common::collectd::fn_produce_with_delay_type &fn_in_queue_produce_with_delay_; // for delayed processing
	const common::collectd::fn_produce_type &fn_out_queue_produce_;
	const common::collectd::fn_produce_with_delay_type &fn_out_queue_produce_with_delay_;
	const common::collectd::fn_remove_from_catalogue_type &fn_remove_from_catalogue_;
	const common::collectd::fn_is_in_catalogue_type &fn_is_in_catalogue_;
	const common::collectd::fn_save_to_database_type &fn_save_to_database_;
	const common::collectd::fn_add_put_group_type &fn_add_put_group_;

	const boost::program_options::variables_map& confMap;

	std::vector<archiving::org_id_t> orgs_enabled_;
	bool orgs_enabled_all_;

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
	 * @param fn_in_queue_consume
	 * @param fn_out_queue_produce
	 * @param fn_remove_from_catalogue
	 * @param fn_is_in_catalogue
	 * @param fn_save_to_database
	 * @param confMap
	 * @param pbx_hostname
	 */
	cdr_inspector(
			const common::collectd::fn_consume_with_timeout_type& fn_in_queue_consume,
			const common::collectd::fn_produce_with_delay_type& fn_in_queue_produce_with_delay,
			const common::collectd::fn_produce_type& fn_out_queue_produce,
			const common::collectd::fn_produce_with_delay_type& fn_out_queue_produce_with_delay,
			const common::collectd::fn_remove_from_catalogue_type& fn_remove_from_catalogue,
			const common::collectd::fn_is_in_catalogue_type& fn_is_in_catalogue,
			const common::collectd::fn_save_to_database_type& fn_save_to_database,
			const common::collectd::fn_add_put_group_type& fn_add_put_group,
			const boost::program_options::variables_map& confMap,
			const std::string& pbx_hostname) :
			fn_in_queue_consume_(fn_in_queue_consume), fn_in_queue_produce_with_delay_(fn_in_queue_produce_with_delay),
			fn_out_queue_produce_(fn_out_queue_produce),
			fn_out_queue_produce_with_delay_(fn_out_queue_produce_with_delay),
			fn_remove_from_catalogue_(fn_remove_from_catalogue), fn_is_in_catalogue_(fn_is_in_catalogue),
			fn_save_to_database_(fn_save_to_database), fn_add_put_group_(fn_add_put_group),
			confMap(confMap), pbx_hostname_(pbx_hostname)
	{
		if(confMap.count("cdr_handler.archiving_enabled_orgs")) {
			const std::string& orgs_enabled(confMap["cdr_handler.archiving_enabled_orgs"].as<std::string>());
			orgs_enabled_all_ = orgs_enabled == "all";

			if(!orgs_enabled_all_ && !orgs_enabled.empty()) {
				std::vector<std::string> orgs_enabled_str;
				boost::algorithm::split(orgs_enabled_str, orgs_enabled, boost::is_any_of(", \t"));
				std::vector<std::string>::const_iterator end = orgs_enabled_str.end();
				for(std::vector<std::string>::const_iterator itr = orgs_enabled_str.begin(); itr != end; ++itr) {
					LogDebug("cdr_handler.archiving_enabled_orgs: "<<*itr);
					orgs_enabled_.push_back(rml::utils::strtoull(itr->c_str()));
				}
			}
		}

	}

	/**
	 * Thread entrypoint
	 */
	void run()
	{
		while(!app::quit) {

			bool enable_rmCDRDiscard = false;
			std::vector<std::string> filter_orgs;
			std::vector<std::string> filtered_xpaths;
			std::string temp_files_base_directory;
			bool filter_orgs_all = false;
			bool chunk_enabled = false;
			std::string chunk_directory;
			bool info_enabled = false;
			std::string info_directory;

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
				chunk_enabled = confMap["chunk.enabled"].as<bool>();
				chunk_directory = confMap["chunk.directory"].as<std::string>();
				info_enabled = confMap["info.enabled"].as<bool>();
				info_directory = confMap["info.directory"].as<std::string>();
			}

			while(!app::quit) {
				try {
					archiving::archivable::pointer p_archivable = fn_in_queue_consume_(false, boost::chrono::milliseconds(5000));
					if (!p_archivable) {
                        // IN queue consume timed out
						continue;
					}

					LogVerbose(*p_archivable<<" from channel");

					archiving::put_group_id_t put_group_id;
					archiving::string_to_uuid(p_archivable->get_uuid(), put_group_id);

					const archiving::file_type::file_type_t&  archivable_file_type(p_archivable->get_type());

					// The next section (not this one) will extract data from CDR files and use it to populate the archivable
					// pointer.  So here, check that we're not dealing with CDRs.  However, if a CDR has already been inspected,
					// it should also be pushed so the out channel (and not inspected in the next section), so also check for that
					// condition here.
					// Check if the file is not a CDR, or if it has already been inspected.
					if((archivable_file_type != archiving::file_type::cdr) || (p_archivable->get_current_state() >= archiving::archivable::state::inspected)) {
						// If if falls in to the above condition, either it's not a CDR, or it has already been inspected.
						// Check that we are in the case where it is not a CDR and it has not yet been inspected.
						// In this case, we are likely to be dealing with a PCAP or a chunked recording.
						if((archivable_file_type != archiving::file_type::cdr) && (p_archivable->get_current_state() < archiving::archivable::state::inspected)) {
							LogVerbose(*p_archivable<<" not a CDR; shifting to out channel");

							p_archivable->set_owners(archiving::owner_info(1));
							p_archivable->set_start_epoch(std::time(0));
							p_archivable->set_add_to_cdrmunch_cache(false);

							// If chunks are enabled and the type is recording...
							if (chunk_enabled && (archivable_file_type == archiving::file_type::recording))
							{
								// ...try extracting info from the filename.
								if (!check_info_in_filename(chunk_directory, p_archivable))
									continue;
							}
							// If info files are enabled and the type is info...
							else if (info_enabled && (archivable_file_type == archiving::file_type::info))
							{
								// ...try extracting info from the filename.
								if (!check_info_in_filename(info_directory, p_archivable))
									continue;
							}
							// fall back to default handling; assign default policy for the type
							else
							{
								p_archivable->set_policy_id(policy::pseudo_id::by_file_type(archivable_file_type));
							}

							p_archivable->set_current_state(archiving::archivable::state::inspected);

							p_archivable->move_to_subdir("collected");
							{
								int retry = 0;
								std::time_t last_attempt(0);
								while(!app::quit) {
									const std::time_t now(std::time(0));
									if(!last_attempt || (now - last_attempt) > 15) {
										if(fn_save_to_database_(p_archivable))
											break;

										last_attempt = now;

										retry++;
										LogMessage(*p_archivable<<" [retry: "<<retry<<"] Save states to DB failed; retrying ...");
									}

									boost::this_thread::sleep(boost::posix_time::seconds(1));
								}

								if(retry && app::quit)
									throw rml::exception("Quit flagged during database retry");
							}
						}
						else
							LogVerbose(*p_archivable<<" Already inspected; shifting to out channel");

						p_archivable->hibernate();

						fn_out_queue_produce_(p_archivable, false);
						continue;
					}

					// If we're here then we're treating the file as a CDR, having checked above for other situations.

					// aliasing it to make the code more readable
					archiving::archivable::pointer archivable_cdr = p_archivable;

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

						const bool call_log_json_export_enabled =
							variables_node.select_single_node("rmCallLogsJSONExportEnabled").node().text().as_bool();

						const archiving::call_duration_based_delayed_producer delayed_producer(
							fn_out_queue_produce_,
							fn_out_queue_produce_with_delay_,
							variables_node.select_single_node("end_epoch").node().text().as_uint(),
							boost::chrono::milliseconds(
								variables_node.select_single_node("mduration").node().text().as_uint()));

						unsigned int recording_count = 0;
						// rmDispSessionRecorded=true means we actually ended up recording in the course of the call progress
						if (variables_node.select_single_node("rmDispSessionRecorded").node().text().as_bool()) {
							recording_count = variables_node.select_single_node("rmCallRecordingCount").node().text().as_uint();
						}

						LogDebug(*archivable_cdr<<" recording_count:" << recording_count << "; call_log_json_export_enabled:" << call_log_json_export_enabled);

						// In the case of a call-log json enabled CDR, since the MD5 of a recording has to be calculated
						// as soon as we identify them via the CDR, there is a chance that (as we have seen in the past
						// with other recordings) the switch has not finished writing the WAV file to disk; i.e. end up
						// calculating wrong MD5.
						//
						// In order to get around that, a delay is calculated based on the end-time and duration.
						//
						// Adds delay only if there are recordings on the call-log JSON export enabled CDR.
						//
						// (Re)using delayed_producer, to calculate delay and make the decision whether to delay or not.
						// If there must be a delay, re-queue the item back into IN queue, using the callback directly.
						if (call_log_json_export_enabled && recording_count > 0 && delayed_producer.must_delay()) {
							LogDebug(*archivable_cdr<<" delaying CDR since call-log JSON is enabled and has recordings");
							fn_in_queue_produce_with_delay_(archivable_cdr, delayed_producer.delay_, false);
							continue;
						}

						const char* cdr_uuid = variables_node.select_single_node("uuid/text()").node().value();
						LogDebug("cdr_uuid: "<<cdr_uuid);

						{
							pugi::xpath_node direction_node = doc.select_single_node("/cdr/channel_data/direction/text()");
							RML_DEBUG(
								if(!direction_node)
									LogDebug(*archivable_cdr<<" /cdr/channel_data/direction NOT found");
								else
									LogDebug(*archivable_cdr<<" /cdr/channel_data/direction = "<<direction_node.node().value());
							);

							pugi::xpath_node rmAssumeALeg_node = variables_node.select_single_node("rmAssumeALeg/text()");
							RML_DEBUG(
								if(!rmAssumeALeg_node)
									LogDebug(*archivable_cdr<<" /cdr/variables/rmAssumeALeg NOT found");
								else
									LogDebug(*archivable_cdr<<" /cdr/variables/rmAssumeALeg = "<<rmAssumeALeg_node.node().value());
							);

							leg = ((direction_node && (0 == strncasecmp("inbound", direction_node.node().value(), 7))) || (rmAssumeALeg_node && (0 == strncasecmp("true", rmAssumeALeg_node.node().value(), 4))))? leg::A : leg::B;
						}

						std::time_t start_epoch = 0;
						{
							pugi::xpath_node start_epoch_node = variables_node.select_single_node("start_epoch/text()");
							if(start_epoch_node) {
								start_epoch = rml::utils::strtoull(start_epoch_node.node().value());
							}
							else {
								LogWarning(*archivable_cdr<<" Start Epoch not found ( /cdr/variables/start_epoch ); falling-back to current time");
								std::time(&start_epoch);
							}
						}

						const char* originator_uuid = 0;
						{
							if(leg == leg::A) {
								originator_uuid = cdr_uuid;
							} else {
								originator_uuid = get_originator_uuid(variables_node, archivable_cdr);
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
									LogDebug(*archivable_cdr<<" From CDR rmOrgID: "<<rmOrgID);
								}
							}

							archiving::user_id_t rmUserID = 0;
							{
								pugi::xpath_node rmUserID_node = variables_node.select_single_node("rmUserID/text()");
								if(rmUserID_node) {
									rmUserID = (archiving::user_id_t)rml::utils::strtoll(rmUserID_node.node().value(), 0);
									LogDebug(*archivable_cdr<<" From CDR rmUserID: "<<rmUserID);
								}
							}

							{
								owners.reset(new archiving::owner_info(rmOrgID? rmOrgID: 1));
								owners->addUser(rmUserID);
								archivable_cdr->set_owners(*owners); // we change this when we send the PUT request @see archivable::inject_put_request()
							}
						}

						if(filter_orgs_all || !filter_orgs.empty() || enable_rmCDRDiscard) {

							try {
								if(enable_rmCDRDiscard) {
									pugi::xpath_node rmCDRDiscard_node = variables_node.select_single_node("rmCDRDiscard/text()");
									if((isDiscardable = (0 == strncasecmp("true", rmCDRDiscard_node.node().value(), 4))))
										LogMessage(*archivable_cdr<<" rmCDRDiscard=true marked discardable");
								}

								if(!isDiscardable) {
									if((leg == leg::B) && (filter_orgs_all || (filter_orgs.end() != std::find(filter_orgs.begin(), filter_orgs.end(), rmOrgID_str)))) {

										const bool& isLegAnswered(0 == strncasecmp("ANSWER", variables_node.select_single_node("endpoint_disposition/text()").node().value(), 6));
										LogVerbose("cdr:"<<cdr_uuid<<" - isLegAnswered:"<<std::iostream::boolalpha<<isLegAnswered);

										if(!isLegAnswered) {
											isDiscardable = true;
											LogMessage(*archivable_cdr<<" Un-ANSWERed B leg marked discardable");
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

							{
								int retry = 0;
								while(!fn_save_to_database_(archivable_cdr) && !app::quit) {
									LogMessage(archivable_cdr<<" [retry: "<<retry<<"] Save states to DB failed; retrying..");
									boost::this_thread::sleep(boost::posix_time::seconds(15));
									retry++;
								}

								if(retry && app::quit)
									throw rml::exception("Quit flagged during database retry");
							}

							fn_remove_from_catalogue_(archivable_cdr);
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

						// Call buffering stuff goes here
						{
							pugi::xpath_node rmBufferedRecording_loc_node;
							if((rmBufferedRecording_loc_node = variables_node.select_single_node("rmBufferedRecording_loc/text()"))) {
								std::string cbr_path(rmBufferedRecording_loc_node.node().value());

								std::string uuid(cbr_path, cbr_path.length() - (36/*uuid*/+4/*.wav*/), 36);
								{
									char cbr_uuid_str[37] = {'\0'};
									unsigned char cbr_uuid[16];
									uuid_generate(cbr_uuid); uuid_unparse(cbr_uuid, cbr_uuid_str); cbr_uuid_str[36] = '\0';

									LogMessage(archivable_cdr<<" Call buffering identified; re-assigning call buffering uuid:"<<uuid<<" to new uuid:"<<cbr_uuid_str);
									uuid = cbr_uuid_str;
								}

								archiving::archivable::pointer archivable_cbr;
								try {
									// new below will throw if the CBR file is missing, leading to archivable_cbr not being set
									archivable_cbr.reset(
											new archiving::archivable(
													uuid, originator_uuid, start_epoch, cbr_path,
													archiving::file_type::buffered_recording, "wav", pbx_hostname_,
													policy::pseudo_id::buffered_recording));
									archivable_cbr->set_leg_uuid(archivable_cdr->get_uuid());
									LogMessage(*archivable_cdr<<" - Call buffering found - "<<*archivable_cbr);
								}
								catch (const rml::utils::tmp_fstream::error& e) {
									const std::string error(e.what());
									const std::string& cbr_path_not_found(cbr_path+" error:No such file or directory");

									// if it is a failure due to a situation other than the one checked below, then throw!
									if (error.length() <= cbr_path_not_found.length() ||
											(0 != error.compare(
													error.length() - cbr_path_not_found.length(),
													cbr_path_not_found.length(),
													cbr_path_not_found)))
									{
										LogDebug(*archivable_cdr<<" Exception did not match; '"<<cbr_path_not_found<<"' != '"<<error<<"'");
										throw;
									}

									// else if the CBR file was not found on disk ...

									// In the light of https://jira.redmatter.com/browse/PLAT-10207, there needs to be extra
									// checks here to make sure that the PBX logic has actually created the file on disk.
									// Apparently there are situations where it is impossible for the PBX to update the CDR
									// with the fact that the buffered recording didn't get created, eventhough it was setup
									// to do so.
									//
									// Quote from Neil B, from comment on the ticket.
									//   ... these appears to be a call flow where an MT call is made and the call is then
									//   diverted to voicemail. This can be easily configured by using a number that is set
									//   up as a choice number and configuring the Numbers.VSIMs_CallConfig table to set
									//   `IfUnavailable_Number` to 1, and the `IfUnavailable_Number` to +16465134491
									//   (which is our voicemail number). Now, if you hang the call up when just before the
									//   voicemail voice kicks in, there will be no recording made, but the channel variable
									//   will have been set. If you let the voicemail start, then all is OK!
									//
									//   ... only solution I can see is for collectd to ignore situation if the following
									//   holds true ...
									//
									//      if /cdr/variables/answer_epoch = 0 &&
									//			preg_match('/^featureVoiceMailLeave_/', '//callflow/extension[0]/@number')
									//		then
									//			... OK if buffer file is missing
									//		endif

									bool is_cbr_file_expected = true;
									{
										pugi::xpath_node xpath_node = variables_node.select_single_node("answer_epoch/text()");
										if (xpath_node && std::string(xpath_node.node().value()) == "0") {
											if (doc.select_single_node("//callflow/extension[1][starts-with(@number, \"featureVoiceMailLeave_\")]")) {
												is_cbr_file_expected = false;
											}
										}
									}

									if (is_cbr_file_expected) {
										LogWarning(*archivable_cdr<<" No buffered recording was found in "<<cbr_path);
										throw;
									}

									LogMessage(archivable_cdr<<" Call buffering WAV file was not found; Ignoring this as"
											" it fits 'MT Call that ended up in voicemail, but cut before answering'"
											" (ref: PLAT-10207)");
								}

								// if archivable_cbr is set; i.e if the file was not missing
								if (archivable_cbr) {
									archivable_cbr->set_owners(*owners);
									archivable_cbr->set_add_to_cdrmunch_cache(false);
									archivable_cbr->set_current_state(archiving::archivable::state::inspected);

									{
										int retry = 0;
										while(!fn_save_to_database_(archivable_cbr) && !app::quit) {
											LogMessage(*archivable_cbr<<" [retry: "<<retry<<"] Save state to DB failed; retrying ...");
											boost::this_thread::sleep(boost::posix_time::seconds(15));
											retry++;
										}

										if(retry && app::quit)
											throw rml::exception("Quit flagged during database retry");
									}
									archivable_cbr->hibernate();
									fn_add_put_group_(put_group_id);
									delayed_producer.produce(archivable_cbr);

									variables_node.append_child("rmBufferedRecording_UUID").append_child(pugi::node_pcdata).set_value(uuid.c_str());
									variables_node.append_child("rmBufferedRecordingVersion").append_child(pugi::node_pcdata).set_value("2");
									modified = true;
								}
							}
						}

						if(orgs_enabled_all_ || (orgs_enabled_.end() != std::find(orgs_enabled_.begin(), orgs_enabled_.end(), owners->orgID))) {

							// switch between the new variables and old
							// if rmCallRecordingCount set, we are in the new scheme
							pugi::xpath_node node;
							if (recording_count > 0) {
								archiving::archivable::pointer archivable_recording;

								LogVerbose(*archivable_cdr<<" Recording count: "<<recording_count);

								const unsigned int recording_call_analysis_count =
										boost::lexical_cast<unsigned int>(get_xpath_single_value(
												variables_node, "rmAnalysisRequestedCount/text()", "0"));

								std::set<std::string> recording_uuids; // to de-duplicate recordings
								while (recording_count--) {

									const std::string& recording_location( get_xpath_single_value(variables_node, boost::str(boost::format("rmCallRecordingLoc_%1%/text()") % recording_count), std::string()));
									const app::policy::id_type& recording_policy( get_xpath_single_value(variables_node, boost::str(boost::format("rmCallRecordingPolicy_%1%/text()") % recording_count), (long long int)app::policy::pseudo_id::recording) );

									const bool& recording_send_email( get_xpath_single_value(variables_node, boost::str(boost::format("rmCallRecordingEmail_%1%/text()") % recording_count), std::string()) == "true" );
									const char* recording_send_email_to_recips = get_xpath_single_value(variables_node, boost::str(boost::format("rmCallRecordingToRecips_%1%/text()") % recording_count), (const char*)0);
									const char* recording_send_email_cc_recips = get_xpath_single_value(variables_node, boost::str(boost::format("rmCallRecordingCcRecips_%1%/text()") % recording_count), (const char*)0);

									if(!recording_location.empty()) {
										try {
											std::string uuid(recording_location, recording_location.length() - (36/*uuid*/+4/*.wav*/), 36);

											// capture the UUID for cdrmunch; steps after this might not succeed as this recording might have been already found and queued for archiving
											variables_node.append_child(boost::str(boost::format("rmCallRecordingUUID_%1%") % recording_count).c_str()).append_child(pugi::node_pcdata).set_value(uuid.c_str());
											modified = true;

											// make sure we are not duplicating
											{
												const std::pair<std::set<std::string>::iterator, bool> res(recording_uuids.insert(uuid));
												if (!res.second || fn_is_in_catalogue_(boost::make_shared<archiving::archivable>(uuid, archiving::file_type::recording))) {
													LogMessage(archivable_cdr<<" Recording [recording/"<<uuid<<"] already being processed; ignoring");
													continue;
												}
											}

											archivable_recording.reset(new archiving::archivable(uuid, originator_uuid, start_epoch, recording_location, archiving::file_type::recording, "wav", pbx_hostname_, recording_policy));
											archivable_recording->set_leg_uuid(archivable_cdr->get_uuid());
											archivable_recording->set_owners(*owners);

											// We could possibly do this for all recordings rather than just for call-log-json enabled ones.
											// Concern is the spike in load on PBX, whether the recording will be sent over to ArchiveD or not.
											if (call_log_json_export_enabled) {
												std::string md5;
												{
													std::ostringstream ostr_md5;
													ostr_md5 << archivable_recording->get_md5();
													md5 = ostr_md5.str();
												}
												variables_node.append_child(boost::str(boost::format("rmCallRecordingMD5_%1%") % recording_count).c_str()).append_child(pugi::node_pcdata).set_value(md5.c_str());
												modified = true;
											}

											LogMessage(*archivable_cdr<<" - Archivable recording found - "<<*archivable_recording);

											if (recording_policy == policy::pseudo_id::do_not_archive
													|| (recording_send_email &&
															((recording_send_email_to_recips && 0 < strlen(recording_send_email_to_recips)) ||
																	(recording_send_email_cc_recips && 0 < strlen(recording_send_email_cc_recips))))
													|| recording_call_analysis_count > 0)
											{
												if (recording_policy == policy::pseudo_id::do_not_archive) {
													LogVerbose(*archivable_recording<<" Found to have 'DO NOT ARCHIVE' policy set");
												}
												if (recording_send_email &&
													((recording_send_email_to_recips && 0 < strlen(recording_send_email_to_recips)) ||
													 (recording_send_email_cc_recips && 0 < strlen(recording_send_email_cc_recips))))
												{
													LogVerbose(*archivable_recording<<" Found to have SEND EMAIL task enabled");
												}
												if (recording_call_analysis_count > 0) {
													LogVerbose(*archivable_recording<<" Found to have CALL ANALYSIS task enabled");
												}

												LogVerbose(*archivable_recording<<" Set to be added to CDRMunch cache");
												archivable_recording->set_add_to_cdrmunch_cache(true);
											}

											archivable_recording->set_current_state(archiving::archivable::state::inspected);
											{
												int retry = 0;
												while(!fn_save_to_database_(archivable_recording) && !app::quit) {
													LogMessage(*archivable_recording<<" [retry: "<<retry<<"] Save state to DB failed; retrying ...");
													boost::this_thread::sleep(boost::posix_time::seconds(15));
													retry++;
												}

												if(retry && app::quit)
													throw rml::exception("Quit flagged during database retry");
											}

											archivable_recording->hibernate();
										}
										catch(const std::exception& e) {
											const std::string& is_recording_active(get_xpath_single_value(variables_node, boost::str(boost::format("rmCallRecordingActive_%1%/text()") % recording_count), std::string("true")));
											if(is_recording_active == "true")
												LogError("Failed to open recording file [rmCallRecordingActive_"<<recording_count<<"=true]; Error:"<<e.what());
											else
												LogVerbose("Recording never started (rmCallRecordingActive_X == false); ignoring.");

											// add a flag to signal this
											variables_node.append_child(boost::str(boost::format("rmCallRecordingUnavailable_%1%") % recording_count).c_str()).append_child(pugi::node_pcdata).set_value("true");

											modified = true;
										}

										if (archivable_recording) {
											fn_add_put_group_(put_group_id);
											delayed_producer.produce(archivable_recording);
										}
									}
								}
							}
							else if((node = doc.select_single_node("/cdr/variables[ rmDispRecordRetain = 'true' or rmDispPersonalRecordRetain = 'true' or rmDispRecordEmailSend = 'true' or rmDispPersonalRecordEmailSend = 'true' ]/rmLocalRecordLocation/text()"))) {

								archiving::archivable::pointer archivable_recording;

								try {
									const app::policy::id_type recording_policy(
											doc.select_single_node("/cdr/variables[ rmDispRecordRetain = 'true' or rmDispPersonalRecordRetain = 'true' ]")?
													policy::pseudo_id::recording: policy::pseudo_id::do_not_archive);

									std::string file_path(node.node().value());
									std::string uuid(file_path, file_path.length() - (36/*uuid*/+4/*.wav*/), 36);
									archivable_recording.reset(
											new archiving::archivable(uuid, originator_uuid, start_epoch, file_path,
													archiving::file_type::recording, "wav", pbx_hostname_, recording_policy));
									archivable_recording->set_leg_uuid(archivable_cdr->get_uuid());

									LogMessage(*archivable_cdr<<" - Archivable recording found - "<<*archivable_recording);

									variables_node.append_child("rmCallRecordingUUID").append_child(pugi::node_pcdata).set_value(uuid.c_str());
									modified = true;

									LogWarning(*archivable_cdr<<" - Recording found via the OLD variables; Beware of CDRMunch incompatibility! file: "<<*archivable_recording);

									{
										// should we add this to CDRMunch Cache ?
										const bool rmDispRecordEmailSend( "false" != get_xpath_single_value(variables_node, "rmDispRecordEmailSend/text()", std::string("false")) );
										const bool rmDispPersonalRecordEmailSend( "false" != get_xpath_single_value(variables_node, "rmDispPersonalRecordEmailSend/text()", std::string("false")) );
										const bool rmDispRecordToRecips(get_xpath_single_value(variables_node, "rmDispRecordToRecips/text()", std::string()).empty());
										const bool rmDispPersonalRecordToRecips(get_xpath_single_value(variables_node, "rmDispPersonalRecordToRecips/text()", std::string()).empty());

										if((rmDispRecordEmailSend && rmDispRecordToRecips) || (rmDispPersonalRecordEmailSend && rmDispPersonalRecordToRecips))
											archivable_recording->set_add_to_cdrmunch_cache(true);
										else if(recording_policy == policy::pseudo_id::do_not_archive) {
											LogWarning(archivable_recording<<" NO recipients specified and NOT requested to retain; failing safe by applying the default recording policy");
											// recording_policy = policy::pseudo_id::recording;
											archivable_recording->set_policy_id(policy::pseudo_id::recording);
										}
									}

									archivable_recording->set_owners(*owners);
									archivable_recording->set_current_state(archiving::archivable::state::inspected);
									{
										int retry = 0;
										while(!fn_save_to_database_(archivable_recording) && !app::quit) {
											LogMessage(archivable_recording<<" [retry: "<<retry<<"] Save state to DB failed; retrying ...");
											boost::this_thread::sleep(boost::posix_time::seconds(15));
											retry++;
										}

										if(retry && app::quit)
											throw rml::exception("Quit flagged during database retry");
									}

									archivable_recording->hibernate();
								}
								catch(const std::exception& e) {
									LogWarning("Failed producing archivable item; error: "<<e.what());

									variables_node.append_child("rmCallRecordingUnavailable").append_child(pugi::node_pcdata).set_value("true");
									modified = true;
								}

								if (archivable_recording) {
									fn_add_put_group_(put_group_id);
									delayed_producer.produce(archivable_recording);
								}
							}
						}
						else
							LogMessage(*archivable_cdr<<" - Any recordings are ignored; handled by fseventfilemonitor");

						// We need to strip out any unwanted tags using filtered_xpaths[]
						if(!filtered_xpaths.empty()) {
							BOOST_FOREACH ( const std::string& xpath, filtered_xpaths ) {
								pugi::xpath_node_set nodes = doc.select_nodes(&xpath[0]);
								for(pugi::xpath_node_set::const_iterator it = nodes.begin(); it != nodes.end(); ++it) {
									(*it).node().parent().remove_child((*it).node());
									modified = true;
								}
							}
						}

						// did we modify the CDR during the above inspection?
						if(modified) {
							rml::utils::tmp_fstream::pointer tmp_fsptr = rml::utils::tmp_fstream::create(archivable_cdr->get_fstream_ptr()->get_filename(true)+'.', 0U, temp_files_base_directory);
							doc.save(*tmp_fsptr, "  ");
							archivable_cdr->set_fstream(tmp_fsptr);
							LogVerbose(*archivable_cdr<<" modified");
						}

						archivable_cdr->set_originator_uuid(originator_uuid);
						archivable_cdr->set_start_epoch(start_epoch);
						archivable_cdr->set_add_to_cdrmunch_cache(true);

						archivable_cdr->set_current_state(archiving::archivable::state::inspected);
						archivable_cdr->move_to_subdir("collected");
						{
							int retry = 0;
							while(!fn_save_to_database_(archivable_cdr) && !app::quit) {
								LogMessage(*archivable_cdr<<" [retry: "<<retry<<"] Save states to DB failed; retrying ...");
								boost::this_thread::sleep(boost::posix_time::seconds(15));
								retry++;
							}

							if(retry && app::quit)
								throw rml::exception("Quit flagged during database retry");
						}

						archivable_cdr->hibernate();
						fn_add_put_group_(put_group_id);
						fn_out_queue_produce_(archivable_cdr, false);
					}
				}
				catch(const archiving::archivable_channel_t::finishing_exception& e) {
					LogVerbose(e.what());
					break;
				}
				catch (const boost::system::system_error &e) {
					if (e.code() == rml::queue::error::make_error_condition(rml::queue::error::runtime_errors::is_finishing)) {
						LogVerbose(e.what());
						break;
					}
					LogWarning("exception:"<<e.what());
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

	static inline const char* get_originator_uuid(const pugi::xml_node& variables_node,
												  const archiving::archivable::pointer& cdr)
	{
		const char* ret;
		pugi::xpath_node node;

		node = variables_node.select_single_node("rmOriginatorUUID/text()");
		if (node) {
			ret = node.node().value();
			LogDebug(*cdr << " OriginatorUUID from /cdr/variables/rmOriginatorUUID " << ret);
			return ret;
		}

		node = variables_node.select_single_node("originator/text()");
		if (node) {
			ret = node.node().value();
			LogDebug(*cdr << " OriginatorUUID from /cdr/variables/originator " << ret);
			return ret;
		}

		LogVerbose(*cdr << " OriginatorUUID NOT found");

		return "";
	}
};

}  // namespace app

#endif /* CDR_INSPECTOR_HPP_ */
