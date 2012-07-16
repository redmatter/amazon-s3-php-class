/*
 * retention_record.hpp
 *
 *  Created on: 11 Mar 2011
 *      Author: dkorah
 */

#ifndef RETENTION_RECORD_HPP_
#define RETENTION_RECORD_HPP_

#include "storage_endpoint.hpp"
#include "types.hpp"

#include "../common/policy.hpp"
#include "../common/types.hpp"
#include "../common/cache.hpp"

#include <boost/shared_ptr.hpp>

#include <set>

namespace app {

struct retention_record
{
	archiving::archivable::identifier identifier;
	types::file_format::file_format_t file_format;
	archiving::user_id_t user_id;
	policy::id_type policy_id;
	policy::version_type policy_version;
	applied_policy::ymd_t creation_date;
	applied_policy::ymd_t retention_min_date;
	applied_policy::ymd_t retention_max_date;
	time_t midnight_offset;
	bool purged;
	bool expired;
	archiving::endpoint_id_t endpoint_id;
	
	typedef boost::shared_ptr<retention_record> pointer;

	retention_record() :
		identifier(),
		file_format(types::file_format::unknown),
		user_id(0),
		policy_id(0),
		policy_version(0),
		creation_date(1970,1,1),
		retention_min_date(1970,1,1),
		retention_max_date(1970,1,1),
		midnight_offset(0),
		purged(false),
		expired(false),
		endpoint_id(0)
	{
		//ctor
	}

	retention_record(
			const archiving::archivable::identifier& identifier,
			const types::file_format::file_format_t file_format,
			const archiving::user_id_t user_id,
			const policy::id_type policy_id,
			const policy::version_type policy_version,
			const applied_policy::ymd_t& creation_date,
			const applied_policy::ymd_t& retention_min_date,
			const applied_policy::ymd_t& retention_max_date,
			const time_t midnight_offset,
			const bool purged,
			const bool expired,
			const archiving::endpoint_id_t endpoint_id
			) :
				identifier(identifier),
				file_format(file_format),
				user_id(user_id),
				policy_id(policy_id),
				policy_version(policy_version),
				creation_date(creation_date),
				retention_min_date(retention_min_date),
				retention_max_date(retention_max_date),
				midnight_offset(midnight_offset),
				purged(purged),
				expired(expired),
				endpoint_id(endpoint_id)
	{
		//ctor
	}
	
	struct pointer_compare
	{
		bool operator()( const pointer& lhs, const pointer& rhs )
		{
			if (lhs->creation_date.year != rhs->creation_date.year)
				return lhs->creation_date.year < rhs->creation_date.year;

			if (lhs->creation_date.month != rhs->creation_date.month)
				return lhs->creation_date.month < rhs->creation_date.month;

			if (lhs->creation_date.day != rhs->creation_date.day)
				return lhs->creation_date.day < rhs->creation_date.day;

			if (lhs->identifier.org != rhs->identifier.org)
				return lhs->identifier.org < rhs->identifier.org;

			if (lhs->identifier.filetype != rhs->identifier.filetype)
				return lhs->identifier.filetype < rhs->identifier.filetype;

			if (lhs->identifier.uuid != rhs->identifier.uuid)
				return lhs->identifier.uuid < rhs->identifier.uuid;
		}
	};

	struct pointer_filetype_compare
	{
		bool operator()( const pointer& lhs, const pointer& rhs )
		{
			return lhs->identifier.filetype < rhs->identifier.filetype;
		}
	};

	typedef cache::result_cache<const archiving::archivable::identifier, pointer, archiving::archivable::identifier::compare> cached_finder_t;
	typedef std::set< pointer, pointer_compare > set_t;
	typedef std::multiset< pointer, pointer_filetype_compare > filetype_grouped_set_t;
};

}  // namespace app

#endif /* RETENTION_RECORD_HPP_ */
