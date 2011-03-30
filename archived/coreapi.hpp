/*
 * coreapi.hpp
 *
 *  Created on: 15 Feb 2011
 *      Author: dkorah
 */

#ifndef COREAPI_HPP_
#define COREAPI_HPP_

#include <boost/network/protocol/http/client.hpp>
#include <boost/thread/mutex.hpp>
//#include <boost/thread/condition_variable.hpp>

#include <pugixml/pugixml.hpp>

#include <rml/logger.h>
#include <rml/exception.h>

namespace app {

namespace impl {

template < typename Tag >
class coreapi
{
	typedef boost::network::http::basic_client< Tag , 1 , 1 > http_client;
	typedef typename http_client::request http_client_request;
	typedef typename http_client::response http_client_response;

	std::string base_url_;
	std::string auth_hash_;

	mutable http_client client_;

	static boost::mutex mutex_;
	static std::string authorization_;
	static time_t auth_attempt_time_;
	static bool authorization_error_;

	inline bool get_authorized() const
	{
		boost::unique_lock<boost::mutex> lock(mutex_);

		if (auth_attempt_time_ && ((time(0) - auth_attempt_time_) < 10))
			return !authorization_error_ && !authorization_.empty();

		if(authorization_.empty() || authorization_error_) {

			authorization_.clear();

			try {
				time(&auth_attempt_time_);

				const std::string& response = get(std::string("/auth/")+auth_hash_, false);

				// run xpath
				pugi::xml_document doc;
				if(!doc.load(response.c_str()))
					throw rml::exception("Unable to load auth response document");

				const char* authResID = doc.select_single_node("/Success/ResourceID/text()").node().value();
				if(!authResID || !strlen(authResID))
					throw rml::exception("Unable to get authorization from CoreAPI");

				// set authorization
				authorization_ = authResID;
				authorization_error_ = false;
			}
			catch(const std::exception& e) {
				LogError("exception: "<<e.what());
				authorization_error_ = true;

				return false;
			}
		}

		return true;
	}

public:
	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("coreapi.url", boost::program_options::value<std::string>()->default_value("http://api.redmatter.com"), "CoreAPI hostname")
				("coreapi.auth_hash", boost::program_options::value<std::string>()->default_value("coreapi.auth_hash not-configured"), "CoreAPI hash for a system user")
//				("coreapi.pool_size",  boost::program_options::value<unsigned int>()->default_value(5), "CoreAPI connection pool size")
				;
	}

	coreapi(const coreapi& copy) :
		base_url_(copy.base_url_),
		auth_hash_(copy.auth_hash_),
		client_(http_client::follow_redirects)
	{
		// ctor
	}

	coreapi(const std::string& base_url, const std::string& auth_hash) :
		base_url_(base_url),
		auth_hash_(auth_hash),
		client_(http_client::follow_redirects)
	{
		// ctor
	}

	coreapi(const boost::program_options::variables_map& confMap) :
		base_url_(confMap["coreapi.url"].as<std::string>()),
		auth_hash_(confMap["coreapi.auth_hash"].as<std::string>()),
		client_(http_client::follow_redirects)
	{
		int attempts = 3;
		while(attempts--) {
			if(get_authorized())
				break;

			if(!attempts)
				throw rml::exception("failed to get authorized with CoreAPI");

			sleep(10);
		}
	}

	std::string get(const std::string& uri, const bool use_authorization = true) const
	{
		int attempts = 2;

		const std::string& url(base_url_+uri);
		LogVerbose("url: "<<url);

		while(attempts--) {
			http_client_request request(url);
			if(use_authorization) {
				if(true != get_authorized())
					throw rml::exception("Not authorized; invalid call sequence");

				request << boost::network::header("Authorization", authorization_);
			}

			http_client_response response;
			try {
				response = client_.get(request);
			}
			catch(const std::exception& e) {
				LogError("Error while performing GET; err:"<<e.what());
				throw;
			}

			int status = response.status();

			LogDebug("response: "<<status<<' '<<response.status_message()<<'\n'<<response.body());
			if(status >= 200 && status < 300)
				return response.body();

			if(use_authorization && (status == 401 || status == 403))
				authorization_error_ = true;
		}

		throw rml::exception("coreapi::get failed after retry");
	}
};

template < typename Tag >
boost::mutex coreapi< Tag >::mutex_;

template < typename Tag >
std::string coreapi< Tag >::authorization_;

template < typename Tag >
time_t coreapi< Tag >::auth_attempt_time_ = 0;

template < typename Tag >
bool coreapi< Tag >::authorization_error_ = true;

}  // namespace impl

typedef impl::coreapi< boost::network::http::tags::http_default_8bit_udp_resolve > coreapi;

}  // namespace app

#endif /* COREAPI_HPP_ */
