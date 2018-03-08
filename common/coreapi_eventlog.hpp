/*
 * coreapi_eventlog.hpp
 *
 *  Created on: 18/10/2012
 *      Author: dkorah
 */

#ifndef COREAPI_EVENTLOG_HPP_
#define COREAPI_EVENTLOG_HPP_

#include "types.hpp"
#include "cache.hpp"

#include <rml/utils.h>
#include <rml/coreapi.hpp>

#include <pugixml/pugixml.hpp>

#include <boost/shared_ptr.hpp>
#include <boost/format.hpp>

#include <string>

#include <time.h>

namespace {

inline const char* get_xpath_single_value(pugi::xml_node& doc, const std::string& xpath, const char* default_value, const bool is_attr = false)
{
	RML_DEBUG( LogDebug("xpath: "<<xpath<<" default_value:"<<default_value) );
	pugi::xpath_node xpath_node = doc.select_single_node(xpath.c_str());
	if(xpath_node) {
		RML_DEBUG(LogDebug(xpath<<'='<<xpath_node.node().value()));
		return is_attr? xpath_node.attribute().value() : xpath_node.node().value();
	}

	RML_DEBUG(
		if (default_value)
			LogDebug(xpath<<'='<<default_value<<" [default_value]");
		else
			LogDebug(xpath<<"=NULL [default_value]");
	);

	return default_value;
}

inline std::string get_xpath_single_value(pugi::xml_node& doc, const std::string& xpath, const std::string& default_value = "", const bool is_attr = false)
{
	RML_DEBUG(LogDebug("xpath: "<<xpath<<" default_value:"<<default_value));
	return get_xpath_single_value(doc, xpath, default_value.c_str(), is_attr);
}

inline unsigned long long get_xpath_single_value(pugi::xml_node& doc, const std::string& xpath, const unsigned long long& default_value = 0ULL, const bool is_attr = false)
{
	RML_DEBUG(LogDebug("xpath: "<<xpath<<" default_value:"<<default_value));
	return rml::utils::strtoull(get_xpath_single_value(doc, xpath, (const char*)0, is_attr), default_value);
}

inline long long get_xpath_single_value(pugi::xml_node& doc, const std::string& xpath, const long long default_value = 0LL, const bool is_attr = false)
{
	RML_DEBUG(LogDebug("xpath: "<<xpath<<" default_value:"<<default_value));
	return rml::utils::strtoll(get_xpath_single_value(doc, xpath, (const char*)0, is_attr), default_value);
}

}

namespace app {

class coreapi_eventlog
{
public:
	typedef uint64_t id_t;
	typedef boost::shared_ptr< coreapi_eventlog > pointer;

	struct levels
	{
		// NOTE: This does not correspond to what similarly named constants in API::EventLog
		enum levels_t
		{
			unknown = 0,

			critical,
			error,
			warning,
			message,
			verbose,
			debug,

			n_levels
		};

		inline static levels_t from_string(const std::string& str)
		{
			if(str == "Critical")
				return critical;
			if(str == "Error")
				return error;
			if(str == "Warning")
				return warning;
			if(str == "Message")
				return message;
			if(str == "Verbose")
				return verbose;
			if(str == "Debug")
				return debug;

			THROW_RML_EXCEPTION("Unknown level:"<<str);
		}

		inline static const char* to_string(const levels_t& lvl)
		{
			if(lvl <= unknown || lvl >= n_levels)
				THROW_RML_EXCEPTION("Unknown level:"<<int(lvl));

			if(lvl == critical)
				return "Critical";
			if(lvl == error)
				return "Error";
			if(lvl == warning)
				return "Warning";
			if(lvl == message)
				return "Message";
			if(lvl == verbose)
				return "Verbose";

			// if(lvl == debug)
			return "Debug";
		}
	};

	typedef cache::result_cache<const std::pair<archiving::org_id_t, id_t>, pointer> cached_finder_t;

	inline static pointer find(rml::coreapi& api, const std::pair<archiving::org_id_t, id_t>& id)
	{
		const rml::coreapi::response& res(api.get(boost::str(boost::format("/eventlog/%1%/%2%") % id.first % id.second)));
		if(res.first != 200)
			return pointer();

		return from_xml(id.first, res.second);
	}

	inline static pointer create(
			rml::coreapi& api,

			const archiving::org_id_t& org,
			levels::levels_t level, const std::string& message,
			const std::string& context, const std::string& action,

			const archiving::user_id_t& user = 0, const archiving::device_id_t& device = 0,
			const std::string& extra_data="",
			const std::string& authorization = std::string(), const std::string& ip = std::string())
	{
		std::ostringstream ostr_body;
		{
			pugi::xml_document doc;
			pugi::xml_node base_node = doc.append_child("EventLog");
			base_node.append_child("Level").append_child(pugi::node_pcdata).set_value(levels::to_string(level));
			base_node.append_child("Context").append_child(pugi::node_pcdata).set_value(context.c_str());
			base_node.append_child("Action").append_child(pugi::node_pcdata).set_value(action.c_str());
			base_node.append_child("Message").append_child(pugi::node_pcdata).set_value(message.c_str());

			if(user)
				base_node.append_child("UserID").append_child(pugi::node_pcdata).set_value(boost::lexical_cast<std::string>(user).c_str());

			if(device)
				base_node.append_child("DeviceID").append_child(pugi::node_pcdata).set_value(boost::lexical_cast<std::string>(device).c_str());

			if(!extra_data.empty())
				base_node.append_child("ExtraData").append_child(pugi::node_pcdata).set_value(extra_data.c_str());

			doc.save(ostr_body, "  ");
		}

		const rml::coreapi::response& res(api.put(boost::str(boost::format("/eventlog/%1%") % org), ostr_body.str(), "application/xml", true, rml::coreapi::headers(), authorization, ip));
		if (res.first != 201)
			throw rml::exception("Failed creating event-log");

		return from_xml(org, res.second);
	}

	const id_t id;
	const time_t time;

	const archiving::org_id_t org;
	const levels::levels_t level;
	const std::string message;
	const std::string context;
	const std::string action;
	/* optional */
	const archiving::user_id_t user;
	const archiving::device_id_t device;
	const std::string extra_data;

private:
	coreapi_eventlog(
			const id_t& id, const time_t& time,

			const archiving::org_id_t& org,
			levels::levels_t level, const std::string& message,
			const std::string& context, const std::string& action,

			const archiving::user_id_t& user = 0, const archiving::device_id_t& device = 0,
			const std::string& extra_data="") :
				id(id), time(time),
				org(org), level(level), message(message), context(context), action(action), user(user), device(device),
				extra_data(extra_data)
	{
	}

	static inline pointer from_xml(const archiving::org_id_t& org, const std::string& xml)
	{
		std::string& xml_(const_cast<std::string&>(xml));

		pugi::xml_document doc;
		pugi::xml_parse_result res = doc.load_buffer_inplace(&xml_[0], xml_.length());
		if(!res) {
			LogWarning("Failed loading API response");
			return pointer();
		}

		pugi::xml_node ev_node;
		{
			pugi::xpath_node node = doc.select_single_node("/EventLog/EventLog");
			if(!node) {
				LogWarning("Failed loading API response; XPath /EventLog/EventLog not found");
				return pointer();
			}

			ev_node = node.node();
		}

		return pointer(new coreapi_eventlog(
				get_xpath_single_value(ev_node, "@ID", 0ULL, true),
				get_xpath_single_value(ev_node, "Time/text()", 0ULL),
				org, levels::from_string(get_xpath_single_value(ev_node, "@Level", "", true)),
				get_xpath_single_value(ev_node, "Message/text()", ""),
				get_xpath_single_value(ev_node, "Context/text()", ""),
				get_xpath_single_value(ev_node, "Action/text()", ""),
				get_xpath_single_value(ev_node, "UserID/text()", 0LL),
				get_xpath_single_value(ev_node, "DeviceID/text()", 0LL),
				get_xpath_single_value(ev_node, "ExtraData/text()", "")));
	}
};

}  // namespace app

namespace std {

inline ostream& operator << (ostream& ostr, const app::coreapi_eventlog::levels::levels_t& l)
{
	return ostr << app::coreapi_eventlog::levels::to_string(l);
}

}  // namespace std

#endif /* COREAPI_EVENTLOG_HPP_ */
