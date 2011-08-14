/*
 * policy.hpp
 *
 *  Created on: 03 Feb 2011
 *      Author: dkorah
 */

#ifndef POLICY_HPP_
#define POLICY_HPP_

#include "coreapi.hpp"
#include "cache.hpp"
#include "types.hpp"

#include <rml/exception.h>

#include <pugixml/pugixml.hpp>

#include <boost/date_time/gregorian/gregorian_types.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/program_options.hpp>
#include <boost/foreach.hpp>
#include <boost/function.hpp>

#include <stdint.h>

namespace app {

/**
 * Defines various retention units
 */
struct retention_units
{
	enum retention_units_t
	{
		unknown = 0,
		days,
		weeks,
		months,
		years,

		n_retention_units
	};

	static const retention_units_t from_string(const std::string& str)
	{
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

	static const char* to_string(const retention_units_t& ru)
	{
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

struct retention_period
{
	typedef uint32_t period_type;

	period_type period;
	retention_units::retention_units_t unit;

	retention_period(const period_type& period, const retention_units::retention_units_t& unit) :
		period(period), unit(unit)
	{
		//ctor
	}

	retention_period() : period(0), unit(retention_units::unknown)
	{
		//ctor
	}

	boost::posix_time::ptime add_to(const boost::posix_time::ptime& pt) const
	{
		switch(unit) {
		case retention_units::days:
			return pt + boost::gregorian::days(period + 1); // additional day to account for time truncation in retention data
			break;

		case retention_units::weeks:
			return pt + boost::gregorian::weeks(period) + boost::gregorian::days(1);
			break;

		case retention_units::months:
			return pt + boost::gregorian::months(period) + boost::gregorian::days(1);
			break;

		case retention_units::years:
			return pt + boost::gregorian::years(period) + boost::gregorian::days(1);
			break;

		default:
			THROW_RML_EXCEPTION("Invalid retention_unit: "<<retention_units::to_string(unit));
		}
	}

	boost::posix_time::ptime add_to(const time_t& t) const
	{
		return add_to(boost::posix_time::from_time_t(t));
	}
};

typedef std::set<archiving::user_id_t> users_t;

struct policy
{
	typedef int64_t id_type; // making it signed to accomodate the negative IDs passed in from FreeSwitch to denote NOT to archive
	typedef uint16_t version_type;

	id_type id;
	version_type version; // for audit purpose ( during purge & GET for audit log )

	retention_period min_retention;
	retention_period max_retention;

	archiving::endpoint_id_t endpoint_id;

	typedef boost::shared_ptr< policy > pointer;

	policy(const id_type& id, const version_type& version,
			const retention_period::period_type& min_retention_period, const retention_units::retention_units_t& min_retention_unit,
			const retention_period::period_type& max_retention_period, const retention_units::retention_units_t& max_retention_unit,
			const archiving::endpoint_id_t& endpoint_id) :
				id(id), version(version),
				min_retention(min_retention_period, min_retention_unit),
				max_retention(max_retention_period, max_retention_unit),
				endpoint_id(endpoint_id)
	{
		//ctor
	}

	static pointer finder(coreapi& api, const std::pair<archiving::org_id_t, id_type>& org_policy)
	{
		std::ostringstream ostr;
		ostr << "/archiving/policy/"<< org_policy.first << '/' << org_policy.second;

		const std::string& response = api.get(ostr.str());

		// run xpath
		pugi::xml_document doc;
		if(!doc.load(response.c_str()))
			throw rml::exception("Unable to load response document");

		pugi::xml_node policy_node = doc.select_single_node("/Archiving/Policy").node();

		const char* id_str = policy_node.select_single_node("ID/text()").node().value();
		const char* version_str = policy_node.select_single_node("Version/text()").node().value();

	    pugi::xpath_node min_retention_period_node = policy_node.select_single_node("RetentionMin");
	    const char* min_retention_period_str = min_retention_period_node.node().select_single_node("text()").node().value();
	    const char* min_retention_period_unit_str = min_retention_period_node.node().attribute("Unit").value();

	    pugi::xpath_node max_retention_period_node = policy_node.select_single_node("RetentionMax");
	    const char* max_retention_period_str = max_retention_period_node.node().select_single_node("text()").node().value();
	    const char* max_retention_period_unit_str = max_retention_period_node.node().attribute("Unit").value();

	    // assign 0 if no endpoint-id set
	    const archiving::endpoint_id_t& endpoint_id( rml::utils::strtoull(policy_node.select_single_node("StorageEndpointID/text()").node().value(), 0) );

		return pointer(new policy(
				rml::utils::strtoull(id_str), rml::utils::strtoull(version_str),
				rml::utils::strtoull(min_retention_period_str), retention_units::from_string(min_retention_period_unit_str),
				rml::utils::strtoull(max_retention_period_str), retention_units::from_string(max_retention_period_unit_str),
				endpoint_id));
	}

	typedef cache::result_cache<const std::pair<archiving::org_id_t, id_type>, pointer> cached_finder_t;
};

struct applied_policy
{
	typedef boost::posix_time::ptime::date_type::ymd_type ymd_t;

	policy::pointer p_policy;
	boost::posix_time::ptime creation_time;

	applied_policy()
	{
		//ugly!!
	}

	applied_policy(policy::pointer policy, const time_t& create_time) :
		p_policy(policy),
		creation_time(boost::posix_time::from_time_t(create_time))
	{
	}

	std::time_t get_creation_epoch() const
	{
		boost::posix_time::ptime epoch(boost::gregorian::date(1970, 1, 1));
		return std::time_t( (creation_time - epoch).total_seconds() );
	}

	ymd_t get_creation_ymd() const
	{
		return creation_time.date().year_month_day();
	}

	ymd_t get_max_retention_ymd() const
	{
		return p_policy->max_retention.add_to(creation_time).date().year_month_day();
	}


	ymd_t get_min_retention_ymd() const
	{
		return p_policy->min_retention.add_to(creation_time).date().year_month_day();
	}

	std::ostream& operator<<(std::ostream& os) const
	{
		const boost::posix_time::ptime::date_type::ymd_type& creation_date(get_creation_ymd());
		const boost::posix_time::ptime::date_type::ymd_type& max_retention_date(get_max_retention_ymd());
		const boost::posix_time::ptime::date_type::ymd_type& min_retention_date(get_min_retention_ymd());
		return os <<
				creation_date.year << creation_date.month << creation_date.day << ',' <<
				max_retention_date.year << max_retention_date.month << max_retention_date.day << ',' <<
				min_retention_date.year << min_retention_date.month << min_retention_date.day << ',' <<
				"[policy:" << p_policy->id << 'v' << p_policy->version << ']';
	}
};

struct access_control_list
{
	policy::id_type id;
	policy::version_type version; // for audit purpose ( during purge & GET for audit log )

	bool owner_allowed;
	users_t users;
	std::map< archiving::group_id_t, users_t > groups;

	typedef boost::shared_ptr< access_control_list > pointer;

	inline bool allows(const archiving::user_id_t& user, const bool is_object_owner) const
	{
		if(owner_allowed && is_object_owner)
			return true;

		{
			users_t::const_iterator end = users.end();
			if(end != users.find(user))
				return true;
		}

		{
			std::map< archiving::group_id_t, users_t >::const_iterator end = groups.end();
			for(std::map< archiving::group_id_t, users_t >::const_iterator itr = groups.begin(); itr != end; ++itr) {
				users_t::const_iterator end = itr->second.end();
				if(end != itr->second.find(user))
					return true;
			}
		}

		return false;
	}
};

class policy_acl_finder
{
	mutable coreapi& api_;

public:
	policy_acl_finder(coreapi& api) : api_(api)
	{
	}

	access_control_list::pointer operator()(const policy::id_type& policy_id) const
	{
		std::ostringstream ostr;
		ostr << "/archivingpolicies/" << policy_id << "/acl";

//		policy::pointer p = policy_finder_(id);

		const std::string& response = api_.get(ostr.str());

		// FIXME run XPath & create definition;
		(void)response;

		access_control_list::pointer acl(new access_control_list());
		acl->id = policy_id;
		acl->version = 1;

		return acl;
	}
};

typedef cache::result_cache<const policy::id_type&, access_control_list::pointer> cached_policy_acl_finder_t;

}  // namespace app

#endif /* POLICY_HPP_ */
