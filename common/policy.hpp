/*
 * policy.hpp
 *
 *  Created on: 03 Feb 2011
 *      Author: dkorah
 */

#ifndef POLICY_HPP_
#define POLICY_HPP_

#include "cache.hpp"
#include "types.hpp"

#include <rml/exception.h>
#include <rml/coreapi.hpp>

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

	static retention_units_t from_string(const std::string& str)
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
			/* no break */
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
	/// pseudo policy ids used in the archiving system
	struct pseudo_id
	{
		enum pseudo_id_t
		{
			recording = 0,
			cdr = -1,
			sms = -2,
			buffered_recording = -3,
			pcap = -4,
			info = -5,
			pcap_sip_only = -6,

			do_not_archive = -999
		};

		static pseudo_id_t by_file_type(const archiving::file_type::file_type_t& f)
		{
			static const pseudo_id_t xlate[archiving::file_type::n_file_types] = {
					/*unknown*/            pseudo_id::do_not_archive,

					/*cdr*/                pseudo_id::cdr,
					/*recording*/          pseudo_id::recording,
					/*sms*/                pseudo_id::sms,
					/*buffered_recording*/ pseudo_id::buffered_recording,
					/*pcap*/               pseudo_id::pcap,
					/*info*/               pseudo_id::info,
					/*pcap_sip_only*/      pseudo_id::pcap_sip_only
			};

			if(f > archiving::file_type::unknown && f < archiving::file_type::n_file_types)
				return xlate[f];

			THROW_RML_EXCEPTION("Unknown filetype: "<<int(f));
		}
	};

	struct mode
	{
		enum mode_t
		{
			loose = 0,
			compliance = 1,

			n_modes
		};

		static mode_t from_string(const std::string& str)
		{
			if(str == "loose")
				return loose;

			if(str == "compliance")
				return compliance;

			THROW_RML_EXCEPTION("Invalid policy mode:"<<str);
		}

		static const char* to_string(mode_t& m)
		{
			if(m < 0 || m >= n_modes)
				THROW_RML_EXCEPTION("Invalid policy mode:"<<int(m));

			static const char* str[] = { "loose", "compliance" };

			return str[m];
		}
	};

	typedef int64_t id_type; // making it signed to accomodate the negative IDs passed in from FreeSwitch to denote NOT to archive
	typedef uint16_t version_type;

	const id_type id;
	const version_type version; // for audit purpose ( during purge & GET for audit log )

	const mode::mode_t type;

	const retention_period min_retention;
	const retention_period max_retention;

	const archiving::endpoint_id_t endpoint_id;

private:
	const bool allow_unrestricted_access_;
	const bool disallow_recording_download_;

	const bool is_acl_type_default;

public:
	const bool allow_unrestricted_access()
	{
		if(is_acl_type_default)
			return allow_unrestricted_access_;

		return true;
	}

	const bool disallow_recording_download()
	{
		if(is_acl_type_default)
			return disallow_recording_download_;

		return true;
	}

	typedef boost::shared_ptr< policy > pointer;

	policy(const id_type& id, const version_type& version,
			const mode::mode_t& type,
			const retention_period::period_type& min_retention_period, const retention_units::retention_units_t& min_retention_unit,
			const retention_period::period_type& max_retention_period, const retention_units::retention_units_t& max_retention_unit,
			const archiving::endpoint_id_t& endpoint_id,
			const bool& allow_unrestricted_access, const bool& disallow_recording_download, const bool& is_acl_type_default) :
				id(id), version(version), type(type),
				min_retention(min_retention_period, min_retention_unit),
				max_retention(max_retention_period, max_retention_unit),
				endpoint_id(endpoint_id),
				allow_unrestricted_access_(allow_unrestricted_access),
				disallow_recording_download_(disallow_recording_download),
				is_acl_type_default(is_acl_type_default)
	{
		//ctor
	}

	static pointer finder(rml::coreapi& api, const std::pair<archiving::org_id_t, id_type>& org_policy) try
	{
		std::ostringstream ostr;
		ostr << "/archiving/policy/"<< org_policy.first << '/' << org_policy.second;

		rml::coreapi::response response(api.get(ostr.str(), true));
		if(response.first != 200) {
			LogCritical("Policy NOT found ( Org/PolicyID: "<<org_policy.first<<'/' << org_policy.second<<" )");
			return pointer();
		}

		// run xpath
		pugi::xml_document doc;
		if(!doc.load(response.second.c_str())) {
			LogCritical("Unable to load response document ( Org/PolicyID: "<<org_policy.first<<'/' << org_policy.second<<" )");
			return pointer();
		}

		pugi::xml_node policy_node = doc.select_single_node("/Archiving/Policy").node();

		const char* id_str = policy_node.select_single_node("ID/text()").node().value();
		const char* version_str = policy_node.select_single_node("Version/text()").node().value();

		const char* type_str = policy_node.select_single_node("Mode/text()").node().value();

		pugi::xpath_node min_retention_period_node = policy_node.select_single_node("RetentionMin");
		const char* min_retention_period_str = min_retention_period_node.node().select_single_node("text()").node().value();
		const char* min_retention_period_unit_str = min_retention_period_node.node().attribute("Unit").value();

		pugi::xpath_node max_retention_period_node = policy_node.select_single_node("RetentionMax");
		const char* max_retention_period_str = max_retention_period_node.node().select_single_node("text()").node().value();
		const char* max_retention_period_unit_str = max_retention_period_node.node().attribute("Unit").value();

		const bool allow_unrestricted_access ( 0 == strcmp("yes", policy_node.select_single_node("AllowUnrestrictedAccess/text()").node().value()) );
		const bool disallow_recording_download ( 0 == strcmp("yes", policy_node.select_single_node("DisallowRecordingDownload/text()").node().value()) );

		// XXX TODO Ugly fudge to get around the new Salesforce.com ACL type. Revisit this when we do refactoring
		const bool is_acl_type_default ( 0 == strcmp("Default", policy_node.select_single_node("ACLType/text()").node().value()) );

		// assign 0 if no endpoint-id set
		const archiving::endpoint_id_t& endpoint_id( rml::utils::strtoull(policy_node.select_single_node("StorageEndpointID/text()").node().value(), 0) );

		return pointer(new policy(
				rml::utils::strtoull(id_str), rml::utils::strtoull(version_str), mode::from_string(type_str),
				rml::utils::strtoull(min_retention_period_str), retention_units::from_string(min_retention_period_unit_str),
				rml::utils::strtoull(max_retention_period_str), retention_units::from_string(max_retention_period_unit_str),
				endpoint_id,
				allow_unrestricted_access,
				disallow_recording_download,
				is_acl_type_default));
	}
	catch(const std::exception& e) {
		LogCritical("Exception: "<<e.what());
		return pointer();
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

	applied_policy(policy::pointer policy, const boost::posix_time::ptime& creation_time) :
		p_policy(policy),
		creation_time(creation_time)
	{
	}

	std::time_t get_creation_epoch() const
	{
		boost::posix_time::ptime epoch(boost::gregorian::date(1970, 1, 1));
		return std::time_t( (creation_time - epoch).total_seconds() );
	}

	std::time_t get_midnight_offset_seconds() const
	{
		boost::posix_time::ptime midnight(creation_time.date());
		return (creation_time - midnight).total_seconds();
	}

	ymd_t get_creation_ymd() const
	{
		return creation_time.date().year_month_day();
	}

	inline ymd_t get_max_retention_ymd() const
	{
		return get_max_retention_time().date().year_month_day();
	}

	inline ymd_t get_min_retention_ymd() const
	{
		return get_min_retention_time().date().year_month_day();
	}

	inline boost::posix_time::ptime get_min_retention_time() const
	{
		return p_policy->min_retention.add_to(creation_time);
	}

	inline boost::posix_time::ptime get_max_retention_time() const
	{
		return p_policy->max_retention.add_to(creation_time);
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
	rml::coreapi& api_;

public:
	policy_acl_finder(rml::coreapi& api) : api_(api)
	{
	}

	access_control_list::pointer operator()(const policy::id_type& policy_id) const
	{
		std::ostringstream ostr;
		ostr << "/archivingpolicies/" << policy_id << "/acl";

//		policy::pointer p = policy_finder_(id);

		const rml::coreapi::response& response(api_.get(ostr.str(), 200U, true));

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
