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
	uint64_t id;
	
	typedef boost::shared_ptr<retention_record> pointer;

	friend inline std::ostream& operator<<(std::ostream& ostr, const retention_record& r)
	{
		return ostr << '{'<<r.identifier<<'('<<r.id<<')'<<' '
				<<types::file_format::to_string(r.file_format)<<','
				<<r.user_id<<','<<r.policy_id<<','<<r.policy_version<<','
				<<r.creation_date.year<<'-'<<r.creation_date.month<<'-'<<r.creation_date.day<<','
				<<r.retention_min_date.year<<'-'<<r.retention_min_date.month<<'-'<<r.retention_min_date.day<<','
				<<r.retention_max_date.year<<'-'<<r.retention_max_date.month<<'-'<<r.retention_max_date.day<<','
				<<r.midnight_offset<<','<<RML_YES_NO(r.purged)<<','<<RML_YES_NO(r.expired)<<','<<r.endpoint_id<<'}';
	}

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
		endpoint_id(0),
		id(0)
	{
		//ctor
	}

	retention_record(
			const archiving::archivable::identifier& identifier,
			const types::file_format::file_format_t& file_format,
			const archiving::user_id_t& user_id,
			const policy::id_type& policy_id,
			const policy::version_type& policy_version,
			const applied_policy::ymd_t& creation_date,
			const applied_policy::ymd_t& retention_min_date,
			const applied_policy::ymd_t& retention_max_date,
			const time_t& midnight_offset,
			const bool& purged,
			const bool& expired,
			const archiving::endpoint_id_t& endpoint_id,
			const uint64_t& id
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
				endpoint_id(endpoint_id),
				id(id)
	{
		//ctor
	}
	
	bool operator==( const retention_record& rec ) const
	{
		return creation_date.year  == rec.creation_date.year &&
				creation_date.month == rec.creation_date.month &&
				creation_date.day   == rec.creation_date.day &&
				policy_id == rec.policy_id &&
				retention_min_date.year  == rec.retention_min_date.year &&
				retention_min_date.month == rec.retention_min_date.month &&
				retention_min_date.day   == rec.retention_min_date.day &&
				retention_max_date.year  == rec.retention_max_date.year &&
				retention_max_date.month == rec.retention_max_date.month &&
				retention_max_date.day   == rec.retention_max_date.day &&
				identifier == rec.identifier &&
				endpoint_id == rec.endpoint_id &&
				id == rec.id;
	}

	bool operator!=( const retention_record& rec ) const
	{
		return !(*this == rec);
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

namespace std {

inline ostream& operator << (ostream& ostr, app::retention_record::pointer r)
{
	if(r)
		return ostr << *r;

	return ostr << "null";
}

}  // namespace std

#endif /* RETENTION_RECORD_HPP_ */
