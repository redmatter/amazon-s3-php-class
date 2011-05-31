/*
 * retention.hpp
 *
 *  Created on: 11 Mar 2011
 *      Author: dkorah
 */

#ifndef RETENTION_HPP_
#define RETENTION_HPP_

#include "../common/policy.hpp"
#include "../common/types.hpp"

namespace app {

struct retention
{
	archiving::org_id org_id;
	archiving::user_id user_id;
	policy::id_type policy_id;
	policy::version_type policy_version;
	applied_policy::ymd_t creation_date;
	applied_policy::ymd_t retention_min_date;
	applied_policy::ymd_t retention_max_date;

	retention() :
		org_id(0),
		user_id(0),
		policy_id(0),
		policy_version(0),
		creation_date(1970,1,1),
		retention_min_date(1970,1,1),
		retention_max_date(1970,1,1)
	{
		//ctor
	}

	retention(
			const archiving::org_id org_id,
			const archiving::user_id user_id,
			const policy::id_type policy_id,
			const policy::version_type policy_version,
			const applied_policy::ymd_t creation_date,
			const applied_policy::ymd_t retention_min_date,
			const applied_policy::ymd_t retention_max_date) :
				org_id(org_id),
				user_id(user_id),
				policy_id(policy_id),
				policy_version(policy_version),
				creation_date(creation_date),
				retention_min_date(retention_min_date),
				retention_max_date(retention_max_date)
	{
		//ctor
	}
};

}  // namespace app

#endif /* RETENTION_HPP_ */
