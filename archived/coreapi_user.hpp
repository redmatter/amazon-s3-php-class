/*
 * coreapi_user.hpp
 *
 *  Created on: 05/04/2012
 *      Author: dkorah
 */

#ifndef COREAPI_USER_HPP_
#define COREAPI_USER_HPP_

#include "../common/types.hpp"
#include "../common/cache.hpp"

namespace app {

struct coreapi_user
{
	const archiving::user_id_t id;
	const archiving::org_id_t org_id;
	const bool is_system_user;

	typedef boost::shared_ptr< coreapi_user > pointer;

	// lookup { authorization, ip }
	typedef std::pair< std::string, std::string > lookup;

	typedef cache::result_cache< const lookup, pointer > cached_finder_t;

	coreapi_user(const archiving::user_id_t& id, const archiving::org_id_t& org_id, const bool& is_system_user) :
		id(id), org_id(org_id), is_system_user(is_system_user)
	{
	}

	static pointer find(rml::coreapi& api, const lookup& param)
	{
		const rml::coreapi::response& res(api.get("/users/me", true, rml::coreapi::headers(), param.first, param.second));
		if(res.status_code != 200 && res.status_code != 404) {
			LogUMessage("Authorization:"<<param.first<<" Invalid", "SSXP");
			return pointer();
		}

		pugi::xml_document doc;
		pugi::xml_parse_result r = doc.load_buffer(res.body.data(), res.body.length());
		if(!r) {
			LogError("Invalid API response for user lookup (/users/me) with authorization:"<<param.first<<" [xml loading failed]");
			return pointer();
		}

		// params for the CTor
		archiving::user_id_t user_id(0);
		archiving::org_id_t user_org_id(0);
		bool is_system_user(false);

		// TODO defudge this at some point, such that the API treats system users as normal and responds with sane XML
		if(res.status_code == 404) {
			const pugi::xpath_node resid_node(doc.select_single_node("/Error/ResourceID/text()"));
			if(resid_node)
				user_id = rml::utils::strtoll(resid_node.node().value(), 0LL);

			is_system_user = (user_id < 0);

			if(!is_system_user) {
				LogCritical("Unexpected API response for user lookup (/users/me) with authorization:"<<param.first<<" [status:404 ResourceID>=0]");
				return pointer();
			}
		}
		else {
			const pugi::xml_node user_node(doc.select_single_node("/Users/User").node());
			user_id = user_node.attribute("ID").as_int();
			user_org_id = user_node.attribute("HomeOrgID").as_uint();
			if (!user_id || !user_org_id) {
				LogError("Invalid API response for user lookup (/users/me) with authorization:"<<param.first<<" [invalid org-id or user-id]");
				return pointer();
			}
		}

		return pointer(new coreapi_user(user_id, user_org_id, is_system_user));
	}
};

}  // namespace app

#endif /* COREAPI_USER_HPP_ */
