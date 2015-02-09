/*
 * suppress_headers_check.hpp
 *
 * A utility class that has the knowledge of various scenarios in which headers-check in retention-purge need to
 * enabled or disabled. It has logic to identify / classify each purge item to fit a scenario. It also has some logic
 * to read settings from a settings-string and translate that into a more efficient boolean array indexed by scenario
 * types.
 *
 *  Created on: 26 Jan 2015
 *      Author: dkorah
 */

#ifndef ARCHIVED_SUPPRESS_HEADERS_CHECK_HPP_
#define ARCHIVED_SUPPRESS_HEADERS_CHECK_HPP_

#include "../common/types.hpp"

#include <rml/exception.h>

#include <boost/algorithm/string.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>

#include <string>
#include <iostream>

namespace app
{

namespace headers_check
{

/**
 * Various scenarios for suppressing the headers check; mostly definitions of enums and some utility functions.
 */
struct scenario
{
	enum scenario_t
	{
		none = 0,

		any_filetype,
		internal_filetype,
		non_internal_filetype,

		any_filetype_with_legacy_headers,
		internal_filetype_with_legacy_headers,
		non_internal_filetype_with_legacy_headers,

		n_scenario
	};

private:
	inline static const char** strings()
	{
		static const char* strings[] = {
			"none",
			"any-filetype",
			"internal-filetype",
			"non-internal-filetype",
			"any-filetype-with-legacy-headers",
			"internal-filetype-with-legacy-headers",
			"non-internal-filetype-with-legacy-headers"
		};

		return strings;
	}

public:
	static inline scenario_t from_string(const std::string& str)
	{
		for (int idx(none); idx < n_scenario; ++idx) {
			if (str == strings()[idx]) {
				return scenario_t(idx);
			}
		}

		THROW_RML_EXCEPTION("from_string: invalid scenario:"<<str);
	}

	static const char* to_string(const scenario_t& s)
	{
		const int idx(s);
		if (idx < none || idx >= n_scenario) {
			THROW_RML_EXCEPTION("to_string: invalid scenario:"<<idx);
		}

		return strings()[idx];
	}

	/**
	 * classify a purge-item detail into one of the above scenarios
	 */
	static inline scenario_t from_components(
			const archiving::file_type::file_type_t& filetype,
			const bool& is_legacy_header)
	{
		const bool& is_internal_filetype(archiving::file_type::is_internal(filetype));
		/*
		std::cout << "is_legacy_header ("<<creation_date<<" < "<<legacy_header_last_date<<") "<<is_legacy_header<<std::endl;
		*/
		if (is_internal_filetype)
			return is_legacy_header? internal_filetype_with_legacy_headers: internal_filetype;

		return is_legacy_header? non_internal_filetype_with_legacy_headers: non_internal_filetype;
	}
};

/**
 * Models the suppressions checker
 */
class suppressions
{
	bool suppressed_scenarios_[scenario::n_scenario];
	const boost::gregorian::date& legacy_header_last_date;

public:
	/**
	 * suppressions checker instantiation from config string which is comma delimited list of suppression scenarios, and
	 * a date epoch that is considered to be the date on which legacy headers were fixed.
	 */
	suppressions(const std::string& suppressed_scenarios_str, const boost::gregorian::date& legacy_header_last_date)
		: legacy_header_last_date(legacy_header_last_date)
	{
		for(int i = 0; i < scenario::n_scenario; i++)
			suppressed_scenarios_[i] = false;

		std::vector<std::string> str_scenarios;
		boost::split(str_scenarios, suppressed_scenarios_str.empty()? "none": suppressed_scenarios_str, boost::is_any_of(", \t"));
		std::vector<std::string>::const_iterator itr = str_scenarios.begin();
		std::vector<std::string>::const_iterator end = str_scenarios.end();
		for (; itr != end; ++itr)
			suppressed_scenarios_[(int)scenario::from_string(*itr)] = true;

		/*
		std::cout << "suppressed_scenarios_\n[";
		for(int i = 0; i < scenario::n_scenario; i++) {
			std::cout << "\n    "<<scenario::to_string(scenario::scenario_t(i))<<" : "<<suppressed_scenarios_[i];
		}
		std::cout << "\n]"<<std::endl;
		*/
	}

	/**
	 * checks the checkable details of a purge-item and gives a Nay or Yay!
	 */
	inline bool is_suppressed(
			const archiving::file_type::file_type_t& filetype,
			const boost::gregorian::date& creation_date) const
	{
		if (suppressed_scenarios_[scenario::none])
			return false;

		if (suppressed_scenarios_[scenario::any_filetype])
			return true;

		const bool& is_legacy_header(creation_date < legacy_header_last_date);
		if (is_legacy_header && suppressed_scenarios_[scenario::any_filetype_with_legacy_headers])
			return true;

		const scenario::scenario_t& found_scenario(
				scenario::from_components(filetype, is_legacy_header));

		/*
		std::cout << "found_scenario : "<<scenario::to_string(found_scenario)<<std::endl;
		*/

		return suppressed_scenarios_[found_scenario];
	}
};

}  // namespace headers_check

}  // namespace app

#endif /* ARCHIVED_SUPPRESS_HEADERS_CHECK_HPP_ */
