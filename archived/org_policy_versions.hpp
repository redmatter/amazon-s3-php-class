/*
 * org_policy_versions.hpp
 *
 *  Created on: Jun 16, 2014
 *      Author: dkorah
 */

#ifndef ORG_POLICY_VERSIONS_HPP_
#define ORG_POLICY_VERSIONS_HPP_

#include "../common/policy.hpp"
#include "../common/types.hpp"
#include "../common/coreapi.hpp"
#include "../common/cache.hpp"

#include <rml/exception.h>
#include <rml/utils.h>
#include <rml/logger.h>

#include <pugixml/pugixml.hpp>

#include <sstream>

namespace app
{

/**
 * class to represent the various policies and their latest version
 * so that we can check versions during purge!
 */
class org_policy_versions
{
public:
	typedef boost::shared_ptr< org_policy_versions > pointer;

	org_policy_versions() : org_(0)
	{
	}

	/**
	 * Find the org's policy versions, feeds the cache
	 *
	 * @param api
	 * @param org
	 * @return
	 */
	static pointer find_from_api(coreapi& api, const archiving::org_id_t& org)
	{
		std::ostringstream ostr;
		ostr << "/archiving/policy/"<< org << "/all";

		coreapi::response response(api.get(ostr.str(), true));
		if(response.first != 200) {
			LogCritical("Policies NOT found ( Org: "<<org<<" )");
			return pointer();
		}

		// run xpath
		pugi::xml_document doc;
		if(!doc.load(response.second.c_str())) {
			LogCritical("Unable to load response document ( Policies Org: "<<org<<" (all) )");
			return pointer();
		}

		return pointer(new org_policy_versions(org, doc));
	}

	typedef cache::result_cache<const archiving::org_id_t, pointer> cached_finder_t;

	/**
	 * Check if the given policy+version is the latest
	 *
	 * @param id
	 * @param version
	 * @return
	 */
	bool is_policy_version_latest(const policy::id_type& id, const policy::version_type& version) const
	{
		const std::map<policy::id_type, policy::version_type>::const_iterator& itr(
				policy_id_version_map_.find(id));
		if (itr == policy_id_version_map_.end())
			THROW_RML_EXCEPTION("Unknown Policy (OrgID/PolicyID) - "<<org_<<'/'<<id);

		return itr->second == version;
	}

private:
	org_policy_versions(const archiving::org_id_t& org, const pugi::xml_document& doc) : org_(org)
	{
		const pugi::xpath_node_set& policy_nodes(doc.select_nodes("/Archiving/Policies/Policy"));
		if (!policy_nodes.empty()) {
			pugi::xpath_node_set::const_iterator itr = policy_nodes.begin();
			const pugi::xpath_node_set::const_iterator& end(policy_nodes.end());
			for (; itr != end; ++itr) {
				const pugi::xpath_node& policy_node(*itr);
				const char* id_str = policy_node.node().select_single_node("ID/text()").node().value();
				const char* version_str = policy_node.node().select_single_node("Version/text()").node().value();
				policy_id_version_map_[rml::utils::strtoull(id_str)] = rml::utils::strtoull(version_str);
			}
		}
	}

	const archiving::org_id_t org_;
	std::map<policy::id_type, policy::version_type> policy_id_version_map_;
};

}  // namespace app

#endif /* ORG_POLICY_VERSIONS_HPP_ */
