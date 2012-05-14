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

	typedef boost::shared_ptr< coreapi_user > pointer;

	// lookup { authorization, ip }
	typedef std::pair< std::string, std::string > lookup;

	typedef cache::result_cache< const lookup, pointer > cached_finder_t;

	coreapi_user(const archiving::user_id_t& id, const archiving::org_id_t& org_id) : id(id), org_id(org_id)
	{
	}

	static pointer find(coreapi& api, const lookup& param)
	{
		const coreapi::response& res(api.get("/users/me", true, coreapi::headers(), param.first, param.second));
		if(res.first != 200) {
			LogUMessage("Authorization:"<<param.first<<" Invalid", "SSXP");
			return pointer();
		}

		pugi::xml_document doc;
		pugi::xml_parse_result r = doc.load_buffer(&res.second[0], res.second.length());
		if(!r) {
			LogError("Invalid API response for user lookup (/users/me) with authorization:"<<param.first<<" [1]");
			return pointer();
		}

		const archiving::user_id_t user_id(doc.select_single_node("/Users/User").node().attribute("ID").as_int());
		const archiving::org_id_t user_org_id(doc.select_single_node("/Users/User").node().attribute("HomeOrgID").as_uint());
		if (!user_id || !user_org_id) {
			LogError("Invalid API response for user lookup (/users/me) with authorization:"<<param.first<<" [2]");
			return pointer();
		}

		return pointer(new coreapi_user(user_id, user_org_id));
	}
};

}  // namespace app

#endif /* COREAPI_USER_HPP_ */
