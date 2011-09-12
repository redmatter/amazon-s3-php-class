/*
 * coreapi.hpp
 *
 *  Created on: 15 Feb 2011
 *      Author: dkorah
 */

#ifndef COREAPI_HPP_
#define COREAPI_HPP_

#include <rml/logger.h>
#include <rml/exception.h>

#include <boost/network/protocol/http.hpp>

#include <pugixml/pugixml.hpp>

#include <boost/thread/mutex.hpp>

#include <utility>
#include <map>

namespace app {

namespace impl {

template < typename Tag >
class coreapi
{
public:
	typedef std::pair< uint16_t, std::string > response;

	typedef std::map< std::string, std::string > headers;

private:
	typedef boost::network::http::basic_client< Tag , 1 , 1 > http_client;
	typedef typename http_client::request http_client_request;
	typedef typename http_client::response http_client_response;

	std::string base_url_;
	std::string auth_hash_;
	std::string ui_version_;

	mutable boost::asio::io_service& io_service_;
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

				headers hdr;
				hdr.insert(std::make_pair<std::string, std::string>("UIVERSION", ui_version_));

				const response& res( get(std::string("/auth/")+auth_hash_, false, hdr) );

				// run xpath
				pugi::xml_document doc;
				if(!doc.load(res.second.c_str()))
					throw rml::exception("Unable to load auth response document");

				const char* authResID = doc.select_single_node("/Success/ResourceID/text()").node().value();
				if(!authResID || !strlen(authResID))
					throw rml::exception("Unable to get authorization from CoreAPI");

				// set authorization
				authorization_ = authResID;
				authorization_error_ = false;
			}
			catch(const std::exception& e) {
				LogError(boost::this_thread::get_id()<<" exception: "<<e.what());
				authorization_error_ = true;

				return false;
			}
		}

		return true;
	}

	inline void init()
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
		ui_version_(copy.ui_version_),
		io_service_(copy.io_service_),
		client_(io_service_)
	{
		init();
	}

	coreapi(const std::string& base_url, const std::string& auth_hash, const std::string& ui_version, boost::asio::io_service& io_service) :
		base_url_(base_url),
		auth_hash_(auth_hash),
		ui_version_(ui_version),
		io_service_(io_service),
		client_(io_service_)
	{
		init();
	}

	coreapi(const boost::program_options::variables_map& confMap, const std::string& ui_version, boost::asio::io_service& io_service) :
		base_url_(confMap["coreapi.url"].as<std::string>()),
		auth_hash_(confMap["coreapi.auth_hash"].as<std::string>()),
		ui_version_(ui_version),
		io_service_(io_service),
		client_(io_service_)
	{
		init();
	}

	/**
	 * logout from API
	 *
	 * @return true on loged-in & logout succeeded
	 */
	inline bool logout() const
	{
		if(!authorization_.empty() && !authorization_error_) {
			try {
				const response& res( delete_("/auth/", true) );
				return (res.first >= 200 && res.first < 300);
			}
			catch(const std::exception& e) {
				LogWarning("Delete failed: exception: "<<e.what());
				return false;
			}
		}
		else
			LogDebug("Logout invalid; "<<(authorization_error_? "Auth error": (authorization_.empty()? "Not authed": "Unknown error")));

		return true;
	}

	/**
	 * GET requests
	 *
	 * @param uri
	 * @param use_authorization
	 * @param h headers
	 *
	 * @return pair< status, body >
	 */
	response get(const std::string& uri, const bool use_authorization = true, const headers& h = headers()) const
	{
		int attempts = 2;

		const std::string& url(base_url_+uri);
		LogVerbose(boost::this_thread::get_id()<<" url: "<<url);

		while(attempts--) {
			http_client_request request(url);
			if(use_authorization) {
				if(true != get_authorized())
					throw rml::exception("Not authorized; invalid call sequence");

				request << boost::network::header("Authorization", authorization_);
				LogDebug(boost::this_thread::get_id()<<" Authorization: "<<authorization_);
			}

			if(!h.empty()) {
				for(headers::const_iterator itr = h.begin(); itr != h.end(); ++itr) {
					request << boost::network::header(itr->first, itr->second);
					LogDebug(boost::this_thread::get_id()<<' '<<itr->first<<": "<<itr->second);
				}
			}

			http_client_response response;
			try {
				response = client_.get(request);
			}
			catch(const std::exception& e) {
				LogError(boost::this_thread::get_id()<<" Error while performing GET; err:"<<e.what());
				throw;
			}

			int status = response.status();

			LogDebug(boost::this_thread::get_id()<<" response: "<<status<<' '<<response.status_message()<<'\n'<<response.body());

			if(use_authorization && ( status == 401 || status == 403 )) {
				authorization_error_ = true;
				continue;
			}

			return std::make_pair<uint16_t, std::string>(status, response.body());
		}

		throw rml::exception("failed after retry");
	}

	/**
	 * Get with expectation!
	 *
	 * @param uri
	 * @param expected_status
	 * @param use_authorization
	 *
	 * @return response
	 */
	inline response get(const std::string& uri, const response::first_type& expected_status, const bool use_authorization = true, const headers& h = headers()) const
	{
		const response& r(get(uri, use_authorization, h));
		if(expected_status && r.first != expected_status) {
			LogError("Unexpected response status; status:"<<r.first<<" (expected status:"<<expected_status<<')');
			throw rml::exception("Unexpected response status");
		}

		return r;
	}

	/**
	 * delete operations
	 *
	 * @param uri
	 * @param use_authorization
	 *
	 * @return pair< status, body >
	 */
	response delete_(const std::string& uri, const bool use_authorization = true, const headers& h = headers()) const
	{
		int attempts = 2;

		const std::string& url(base_url_+uri);
		LogVerbose(boost::this_thread::get_id()<<" url: "<<url);

		while(attempts--) {
			http_client_request request(url);
			if(use_authorization) {
				if(true != get_authorized())
					throw rml::exception("Not authorized; invalid call sequence");

				request << boost::network::header("Authorization", authorization_);
				LogDebug(boost::this_thread::get_id()<<" Authorization: "<<authorization_);
			}

			if(!h.empty()) {
				for(headers::const_iterator itr = h.begin(); itr != h.end(); ++itr) {
					request << boost::network::header(itr->first, itr->second);
					LogDebug(boost::this_thread::get_id()<<' '<<itr->first<<": "<<itr->second);
				}
			}

			http_client_response response;
			try {
				response = client_.delete_(request);
			}
			catch(const std::exception& e) {
				LogError(boost::this_thread::get_id()<<" Error while performing DELETE; err:"<<e.what());
				throw;
			}

			int status = response.status();

			LogDebug(boost::this_thread::get_id()<<" response: "<<status<<' '<<response.status_message()<<'\n'<<response.body());

			if(use_authorization && ( status == 401 || status == 403 )) {
				if (uri.substr(0, 6) == "/auth/") {
					LogWarning("DELETE "<<uri<<" failed");
					throw rml::exception("DELETE failed");
				}

				authorization_error_ = true;
				continue;
			}

			return std::make_pair<uint16_t, std::string>(status, response.body());
		}

		throw rml::exception("failed after retry");
	}

	/**
	 * POST requests
	 *
	 * @param uri
	 * @param body
	 * @param content_type
	 * @param use_authorization
	 *
	 * @return pair< status, body >
	 */
	response post(const std::string& uri, const std::string& body, const std::string& content_type = "application/xml", const bool use_authorization = true, const headers& h = headers()) const
	{
		int attempts = 2;

		const std::string& url(base_url_+uri);
		LogVerbose(boost::this_thread::get_id()<<" url: "<<url);

		while(attempts--) {
			http_client_request request(url);
			if(use_authorization) {
				if(true != get_authorized())
					throw rml::exception("Not authorized; invalid call sequence");

				request << boost::network::header("Authorization", authorization_);
				LogDebug(boost::this_thread::get_id()<<" Authorization: "<<authorization_);
			}

			if(!h.empty()) {
				for(headers::const_iterator itr = h.begin(); itr != h.end(); ++itr) {
					request << boost::network::header(itr->first, itr->second);
					LogDebug(boost::this_thread::get_id()<<' '<<itr->first<<": "<<itr->second);
				}
			}

			LogDebug(boost::this_thread::get_id()<<" Content-Type: "<<content_type);
			LogDebug(boost::this_thread::get_id()<<" Content-Length: "<<body.length());
			LogDebug(boost::this_thread::get_id()<<" body: "<<body);

			request << boost::network::header("Content-Type", content_type);
			{
				std::ostringstream ostr;
				ostr << body.length();
				request << boost::network::header("Content-Length", ostr.str());
			}
			request << boost::network::body(body);

			http_client_response response;
			try {
				response = client_.post(request);
			}
			catch(const std::exception& e) {
				LogError(boost::this_thread::get_id()<<" Error while performing POST; err:"<<e.what());
				throw;
			}

			int status = response.status();

			LogDebug(boost::this_thread::get_id()<<" response: "<<status<<' '<<response.status_message()<<'\n'<<response.body());

			if(use_authorization && ( status == 401 || status == 403 )) {
				authorization_error_ = true;
				continue;
			}

			return std::make_pair<uint16_t, std::string>(status, response.body());
		}

		throw rml::exception("failed after retry");
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
