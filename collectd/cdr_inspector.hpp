/*
 * cdr_inspector.hpp
 *
 *  Created on: 25 Jan 2011
 *	  Author: dkorah
 */

#ifndef CDR_INSPECTOR_HPP_
#define CDR_INSPECTOR_HPP_

#include <boost/noncopyable.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>

#include <pugixml/pugixml.hpp>

#include "../common/types.hpp"

#include "globals.h"

namespace app {

class cdr_inspector : public boost::noncopyable {

	archiving::archivable_channel_t& in_channel;
	archiving::archivable_channel_t& out_channel;
	const boost::program_options::variables_map& confMap;

	bool was_HUPed;

public:
	static void describe_config(boost::program_options::options_description& optsDesc) {
		optsDesc.add_options()
				("cdr_handler.filter_orgs", boost::program_options::value<std::string>()->default_value(""), "OrgIDs for which filter is turned on")
				("cdr_handler.honour_cdr_variable_rmCDRDiscard", boost::program_options::value<std::string>()->default_value("false"), "honour variable rmCDRDiscard in CDRs")
				("cdr_handler.filtered_xpaths", boost::program_options::value<std::string>()->default_value(""), "filtered xpaths in CDRs")
				("cdr_handler.number_of_threads", boost::program_options::value<unsigned int>()->default_value(2), "number of cdr inspector threads")
				;
	}

	cdr_inspector(archiving::archivable_channel_t& in_channel, archiving::archivable_channel_t& out_channel, const boost::program_options::variables_map& confMap) :
		in_channel(in_channel)
		, out_channel(out_channel)
		, confMap(confMap)
		, was_HUPed(false) { }

	void run() {
		while(!app::quit) {

			bool enable_rmCDRDiscard = false;
			std::vector<std::string> filter_orgs;
			std::vector<std::string> filtered_xpaths;
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

				// TODO setup HUP hook
				if(was_HUPed) {
					LogMessage(__func__<<" HUPed; configuration reloaded");
					was_HUPed = false;
				}

			}

			while(!was_HUPed && !app::quit) {
				try {
					archiving::archivable::pointer archivable_cdr = in_channel.consume();
					LogVerbose(archivable_cdr->get_file_path()<<": md5: "<<archivable_cdr->get_md5());

					// inspect and cleanup CDR
					{
						archiving::archivable::ifstream_ptr ifs = archivable_cdr->get_file_ifstream_ptr();
						ifs->rdbuf()->pubseekpos(0,std::ios::in);

						bool modified = false;
						bool isDiscardable = false;

						pugi::xml_document doc;
						if(!doc.load(*ifs))
							throw rml::exception("Unable to load CDR for filtering");

						const bool isAleg( bool(doc.select_single_node("/cdr/channel_data[ direction = 'outbound' ]")) );
						const char* cdr_uuid = doc.select_single_node("/cdr/variables/uuid/text()").node().value();

						const char* rmOrgID_str = doc.select_single_node("/cdr/variables/rmOrgID/text()").node().value();
						LogVerbose("rmOrgID_str: "<<rmOrgID_str);
						archiving::org_id rmOrgID = 0;
						{
							char *end = 0;
							unsigned long val = strtoul(rmOrgID_str, &end, 10);
							if ((errno == ERANGE) || (errno != 0 && val == 0) || (rmOrgID_str == end))
								LogError("Invalid OrgID: "<<rmOrgID_str);
							else
								rmOrgID = (archiving::org_id)val;
						}
						archiving::user_id rmUserID = 0;
						{
							pugi::xpath_node rmUserID_node = doc.select_single_node("/cdr/variables/rmUserID/text()");
							if(rmUserID_node) {
								const char *str = rmUserID_node.node().value();
								char *end = 0;
								double val = strtod(str, &end);
								if ((errno == ERANGE) || (errno != 0 && val == 0) || (str == end))
									LogError("Invalid userID: "<<str);
								else
									rmUserID = (archiving::user_id)val;
							}
						}

						archiving::owner_info owner(rmOrgID);
						owner.addUser(rmUserID);

						archivable_cdr->set_owners(owner);

						if(filter_orgs_all || !filter_orgs.empty() || enable_rmCDRDiscard) {

							try {
								if(enable_rmCDRDiscard && (0 == strncasecmp("true", doc.select_single_node("/cdr/variables/rmCDRDiscard/text()").node().value(), 4))) {
									LogVerbose("cdr:"<<cdr_uuid<<" - rmCDRDiscard=true marked discardable");
									isDiscardable = true;
								}

								if(!isDiscardable) {
									if(filter_orgs_all || (filter_orgs.end() != std::find(filter_orgs.begin(), filter_orgs.end(), rmOrgID_str))) {

										const bool& rmAssumeALeg(0 == strncasecmp("true", doc.select_single_node("/cdr/variables/rmAssumeALeg/text()").node().value(), 4));
										const bool& isLegAnswered(0 == strncasecmp("ANSWER", doc.select_single_node("/cdr/variables/endpoint_disposition/text()").node().value(), 6));

										LogVerbose("cdr:"<<cdr_uuid<<" - rmAssumeALeg:"<<std::iostream::boolalpha<<rmAssumeALeg);
										LogVerbose("cdr:"<<cdr_uuid<<" - isLegAnswered:"<<std::iostream::boolalpha<<isLegAnswered);

										isDiscardable = (!isAleg && !rmAssumeALeg && !isLegAnswered);
										if(isDiscardable)
											LogVerbose("cdr:"<<cdr_uuid<<" - Un-ANSWERed B leg marked discardable");
									}
								}
							}
							catch(const std::exception& e) {
								LogError(__func__<<": "<<e.what());
								throw;
							}
						}

						if( isDiscardable ) {
							archivable_cdr->set_deletable();
							archivable_cdr.reset();
							continue;
						}

						// still the ifs and doc are intact; lets inspect it before we modify it
						// saves another XML parsing!

						// ( $cdr/variables/rmDispRecordRetain = 'true' or $cdr/variables/rmDispPersonalRecordRetain = 'true' )
						// and $cdr/variables/rmDispSessionRecorded = 'true'

						// $cdr/variables/rmDispRecordFileLocation
						pugi::xpath_node node = doc.select_single_node("/cdr/variables[ rmDispRecordRetain = 'true' or rmDispPersonalRecordRetain = 'true' ]/rmDispRecordFileLocation/text()");
						if(node) {
							LogMessage(archivable_cdr->get_file_path()<<" - Archivable recording found.");
							archiving::archivable::pointer archivable_recording(new archiving::archivable(node.node().value(), archiving::archivable::file_type::call_recording, archivable_cdr));
							archivable_recording->set_owners(owner);
							out_channel.produce(archivable_recording);
						}

						// OK, now we have a CDR that has to be POSTed
						// We need to strip out any unwanted tags using filtered_xpaths[]
						if(!filtered_xpaths.empty()) {
							BOOST_FOREACH ( const std::string& xpath, filtered_xpaths ) {
								pugi::xpath_node_set nodes = doc.select_nodes(&xpath[0]);
								for(pugi::xpath_node_set::const_iterator it = nodes.begin(); it != nodes.end(); ++it) {
									(*it).node().parent().remove_child((*it).node());
									modified = true;
								}

								if(modified) {
									archiving::archivable::tmp_file_ptr tmp_file_ptr(new rml::utils::tmp_file("arc-cdr"));

									if(!doc.save_file(tmp_file_ptr->getName().c_str(), "  "))
										throw rml::exception("failed saving filtered CDR");

									archivable_cdr->set_file_path(tmp_file_ptr->getName(), tmp_file_ptr);
									LogVerbose("Modified: "<<archivable_cdr->get_file_path()<<": md5: "<<archivable_cdr->get_md5());
								}
							}
						}

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
