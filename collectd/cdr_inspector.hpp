/*
 * cdr_inspector.hpp
 *
 *  Created on: 25 Jan 2011
 *	  Author: dkorah
 */

#ifndef CDR_INSPECTOR_HPP_
#define CDR_INSPECTOR_HPP_

#include "globals.h"

#include "../common/types.hpp"

#include <rml/utils.h>

#include <pugixml/pugixml.hpp>

#include <boost/noncopyable.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

namespace app {

class cdr_inspector : public boost::noncopyable
{

	archiving::archivable_channel_t& in_channel;
	archiving::archivable_channel_t& out_channel;
	const boost::program_options::variables_map& confMap;

	bool was_HUPed;

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
	cdr_inspector(archiving::archivable_channel_t& in_channel, archiving::archivable_channel_t& out_channel, const boost::program_options::variables_map& confMap) :
		in_channel(in_channel)
		, out_channel(out_channel)
		, confMap(confMap)
		, was_HUPed(false)
	{
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

				// TODO setup HUP hook
				if(was_HUPed) {
					LogMessage("HUPed; configuration reloaded");
					was_HUPed = false;
				}

			}

			while(!was_HUPed && !app::quit) {
				try {
					archiving::archivable::pointer archivable_cdr = in_channel.consume();
					LogVerbose("cdr_inspector::run - got from channel "<<*archivable_cdr);

					// inspect and cleanup CDR
					{
						rml::utils::tmp_fstream::pointer ifs = archivable_cdr->get_fstream_ptr();
						ifs->rdbuf()->pubseekpos(0,std::ios::in);

						bool modified = false;
						bool isDiscardable = false;
						leg::type_t leg;

						pugi::xml_document doc;
						if(!doc.load(*ifs))
							throw rml::exception("Unable to load CDR for filtering");

						const char* cdr_uuid = doc.select_single_node("/cdr/variables/uuid/text()").node().value();

						{
							pugi::xpath_node direction_node = doc.select_single_node("/cdr/channel_data/direction/text()");
							pugi::xpath_node rmAssumeALeg_node = doc.select_single_node("/cdr/variables/rmAssumeALeg/text()");
							leg = ((direction_node && (0 == strncasecmp("inbound", direction_node.node().value(), 7))) || (rmAssumeALeg_node && (0 == strncasecmp("true", rmAssumeALeg_node.node().value(), 4))))? leg::A : leg::B;
						}

						time_t start_epoch = 0;
						{
							pugi::xpath_node start_epoch_node = doc.select_single_node("/cdr/variables/start_epoch/text()");
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
								pugi::xpath_node OriginatorUUID_node = doc.select_single_node("/cdr/variables/rmOriginatorUUID/text()");
								if(OriginatorUUID_node) {
									originator_uuid = OriginatorUUID_node.node().value();
									LogVerbose("OriginatorUUID from /cdr/variables/rmOriginatorUUID "<<originator_uuid);
								}
								else if(pugi::xpath_node originator_node = doc.select_single_node("/cdr/variables/originator/text()")) {
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
							archiving::org_id rmOrgID = 0;
							{
								pugi::xpath_node rmOrgID_node = doc.select_single_node(leg == leg::A? "/cdr/variables/rmOrgID/text()": "/cdr/variables/rmOrgID/text()");
								if(rmOrgID_node) {
									rmOrgID = (archiving::org_id)rml::utils::strtoull(( rmOrgID_str = rmOrgID_node.node().value() ), 0);
									LogVerbose("cdr:"<<cdr_uuid<<" - rmOrgID: "<<rmOrgID);
								}
							}

							if(!rmOrgID) {
								LogError("cdr:"<<cdr_uuid<<" - No rmOrgID found");
								// TODO archivable_cdr->set_done();
								archivable_cdr.reset();
								continue;
							}

							archiving::user_id rmUserID = 0;
							{
								pugi::xpath_node rmUserID_node = doc.select_single_node("/cdr/variables/rmUserID/text()");
								if(rmUserID_node) {
									rmUserID = (archiving::user_id)rml::utils::strtoll(rmUserID_node.node().value(), 0);
									LogVerbose("cdr:"<<cdr_uuid<<" - rmUserID: "<<rmUserID);
								}
							}

							{
								owners.reset(new archiving::owner_info(rmOrgID));
								owners->addUser(rmUserID);
								archivable_cdr->set_owners(*owners);
							}
						}

						if(filter_orgs_all || !filter_orgs.empty() || enable_rmCDRDiscard) {

							try {
								if(enable_rmCDRDiscard) {
									pugi::xpath_node rmCDRDiscard_node = doc.select_single_node("/cdr/variables/rmCDRDiscard/text()");
									if((isDiscardable = (0 == strncasecmp("true", rmCDRDiscard_node.node().value(), 4))))
										LogMessage("cdr:"<<cdr_uuid<<" - rmCDRDiscard=true marked discardable");
								}

								if(!isDiscardable) {
									if((leg == leg::B) && (filter_orgs_all || (filter_orgs.end() != std::find(filter_orgs.begin(), filter_orgs.end(), rmOrgID_str)))) {

										const bool& isLegAnswered(0 == strncasecmp("ANSWER", doc.select_single_node("/cdr/variables/endpoint_disposition/text()").node().value(), 6));
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
							// TODO archivable_cdr->set_done();
							archivable_cdr.reset();
							continue;
						}

						// still the ifs and doc are intact; lets inspect it before we modify it
						// saves another XML parsing!


						// switch between the new variables and old
						// if rmCallRecordingCount set, we are in the new scheme
						pugi::xpath_node node;
						if((node = doc.select_single_node("/cdr/variables/rmCallRecordingCount/text()"))) {
							archiving::archivable::pointer archivable_recording;
							pugi::xpath_node recording_node;
							pugi::xpath_node policy_node;

							unsigned int recording_count( rml::utils::strtoull(node.node().value()) );
							for(; recording_count; recording_count--) {
								const std::string recording_xpath_str(boost::str(boost::format("/cdr/variables/rmCallRecordingLoc_%1%") % recording_count));
								const std::string policy_xpath_str(boost::str(boost::format("/cdr/variables/rmCallRecordingPolicy_%1%") % recording_count));
								if((recording_node = doc.select_single_node(recording_xpath_str.c_str())) && (policy_node = doc.select_single_node(policy_xpath_str.c_str()))) {
									std::string file_path(recording_node.node().value());
									std::string uuid(file_path, file_path.length() - (36/*uuid*/+4/*.wav*/), 36);
									app::policy::id_type policy_id(rml::utils::strtoull(policy_node.node().value()));
									archivable_recording.reset(new archiving::archivable(uuid, originator_uuid, start_epoch, file_path, archiving::archivable::file_type::recording, "wav", archivable_cdr, policy_id));

									archivable_recording->set_owners(*owners);

									LogMessage(*archivable_cdr<<" - Archivable recording found - "<<*archivable_recording);

									out_channel.produce(archivable_recording);
								}
							}
						}
						else if((node = doc.select_single_node("/cdr/variables[ rmDispRecordRetain = 'true' or rmDispPersonalRecordRetain = 'true' ]/rmLocalRecordLocation/text()"))) {
							// ( $cdr/variables/rmDispRecordRetain = 'true' or $cdr/variables/rmDispPersonalRecordRetain = 'true' )
							// and $cdr/variables/rmDispSessionRecorded = 'true'

							// $cdr/variables/rmLocalRecordLocation
							// Pick the file from the /var/spool location NOT rmfs

							archiving::archivable::pointer archivable_recording;

							LogMessage(*archivable_cdr<<" - Archivable recording found.");

							std::string file_path(node.node().value());
							std::string uuid(file_path, file_path.length() - (36/*uuid*/+4/*.wav*/), 36);
							archivable_recording.reset(new archiving::archivable(uuid, originator_uuid, start_epoch, file_path, archiving::archivable::file_type::recording, "wav", archivable_cdr, 0));

							archivable_recording->set_owners(*owners);

							LogMessage(*archivable_cdr<<" - Archivable recording found - "<<*archivable_recording);

							out_channel.produce(archivable_recording);
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
						out_channel.produce(archivable_cdr);
					}
				}
				catch(const archiving::archivable_channel_t::finishing_exception& e) {
					LogVerbose("cdr_inspector::run - "<<e.what());
					break;
				}
				catch(const std::exception& e) {
					LogError("cdr_inspector::run - exception:"<<e.what());
				}
				catch(...) {
					LogCritical("cdr_inspector::run - unexpected exception");
				}
			}
		}
		LogMessage("cdr_inspector::run - finishing");
	}
};

}  // namespace app

#endif /* CDR_INSPECTOR_HPP_ */
