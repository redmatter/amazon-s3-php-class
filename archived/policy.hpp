/*
 * policy.hpp
 *
 *  Created on: 03 Feb 2011
 *      Author: dkorah
 */

#ifndef POLICY_HPP_
#define POLICY_HPP_

#include <stdint.h>

#include <boost/date_time/gregorian/gregorian_types.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/program_options.hpp>
#include <boost/foreach.hpp>

#include <rml/exception.h>

#include "coreapi.hpp"

namespace app {

struct retention_units {
	enum retention_units_t {
		unknown = 0,
		days,
		weeks,
		months,
		years,

		n_retention_units
	};

	static const retention_units_t& from_string(const std::string& str) {
		if(str == "days")
			return days;
		else if(str == "weeks")
			return weeks;
		else if(str == "months")
			return months;
		else if(str == "years")
			return years;

		return unknown;
	}

	static const char* to_string(const retention_units_t& ru) {
		static const char* str[retention_units::n_retention_units] = {
				"unknown",
				"days",
				"weeks",
				"months",
				"years"
		};

		if(ru > unknown && ru < n_retention_units)
			return str[ru];

		return str[unknown];
	}
};

struct retention_period {
	unsigned short int period;
	retention_units::retention_units_t unit;

	retention_period(unsigned short int& period, const retention_units::retention_units_t& unit) :
		period(period), unit(unit)
	{
	}

	retention_period() : period(0), unit(retention_units::unknown)
	{
	}

	static boost::posix_time::ptime add(const boost::posix_time::ptime& pt, const retention_period& rp) {
		switch(rp.unit) {
		case retention_units::days:
			return pt + boost::gregorian::days(rp.period + 1); // additional day to account for time truncation in retention data
			break;

		case retention_units::weeks:
			return pt + boost::gregorian::weeks(rp.period) + boost::gregorian::days(1);
			break;

		case retention_units::months:
			return pt + boost::gregorian::months(rp.period) + boost::gregorian::days(1);
			break;

		case retention_units::years:
			return pt + boost::gregorian::years(rp.period) + boost::gregorian::days(1);
			break;

		default:
			THROW_RML_EXCEPTION("Invalid retention_unit: "<<retention_units::to_string(rp.unit));
		}
	}
};

typedef std::set<archiving::user_id> users_t;

struct access_control_list {
	uint64_t id;
	uint16_t version; // for audit purpose ( during purge & GET for audit log )

	bool owner_allowed;
	users_t users;
	std::map< archiving::group_id, users_t > groups;

	typedef boost::shared_ptr< access_control_list > pointer;

	inline bool allows(const archiving::user_id& user, const bool is_object_owner) const
	{
		if(owner_allowed && is_object_owner)
			return true;

		{
			users_t::const_iterator end = users.end();
			if(end != users.find(user))
				return true;
		}

		{
			std::map< archiving::group_id, users_t >::const_iterator end = groups.end();
			for(std::map< archiving::group_id, users_t >::const_iterator itr = groups.begin(); itr != end; ++itr) {
				users_t::const_iterator end = itr->second.end();
				if(end != itr->second.find(user))
					return true;
			}
		}

		return false;
	}
};

struct policy {
	uint64_t id;
	uint16_t version; // for audit purpose ( during purge & GET for audit log )

	retention_period min_retention;
	retention_period max_retention;

	typedef boost::shared_ptr< policy > pointer;
};

class policy_finder {
	mutable coreapi api_;
public:
	policy_finder (coreapi& api) : api_(api)
	{
	}

	policy::pointer operator()(const uint64_t& id) const {
		std::ostringstream ostr;
		ostr << "/archivingpolicies/" << id;

		const std::string& response = api_.get(ostr.str());

		// FIXME run XPath & create definition;

		policy::pointer p(new policy());
		p->id = id;
		p->version = 1;
		p->min_retention = retention_period(6, retention_units::months);
		p->max_retention = retention_period(1, retention_units::years);

		return p;
	}
};


class policy_acl_finder {
	mutable coreapi& api_;
//	boost::function< policy::pointer(const uint64&) > policy_finder_;

public:
//	policy_acl_finder(coreapi& api, const boost::function< policy::pointer(const uint64&) >& policy_finder) : api_(api), policy_finder_(policy_finder)
	policy_acl_finder(coreapi& api) : api_(api)
	{
	}

	access_control_list::pointer operator()(const uint64_t& policy_id) const {
		std::ostringstream ostr;
		ostr << "/archivingpolicies/" << policy_id << "/acl";

//		policy::pointer p = policy_finder_(id);

		const std::string& response = api_.get(ostr.str());

		// FIXME run XPath & create definition;

		access_control_list::pointer acl(new access_control_list());
		acl->id = policy_id;
		acl->version = 1;

		return acl;
	}
};


}  // namespace app

#endif /* POLICY_HPP_ */
