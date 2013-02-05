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
#include <boost/asio.hpp>

#include <utility>
#include <map>

namespace app {

extern volatile bool quit;

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

	boost::asio::io_service& io_service_;
	mutable http_client client_;

	static boost::mutex mutex_;
	static std::string ip_;
	static std::string authorization_;
	static time_t auth_attempt_time_;
	static bool authorization_error_;

	inline std::string get_my_ip() const
	{
		try {
			const size_t& hostname_begin(base_url_.find('/')+2);
			const size_t& hostname_end(base_url_.find('/', hostname_begin));

			std::string api_host(base_url_, hostname_begin, hostname_end - hostname_begin);
			std::string api_protocol(base_url_, 0, base_url_.find(':'));
			{
				const size_t& proto_colon(api_host.rfind(':'));
				if(proto_colon != std::string::npos) {
					api_protocol = std::string(api_host, proto_colon+1);
					api_host.erase(proto_colon);
				}
			}

			// connect to API and get local IP
		    boost::asio::ip::tcp::resolver resolver(io_service_);
		    boost::asio::ip::tcp::resolver::query query(boost::asio::ip::tcp::v4(), api_host, api_protocol);
		    boost::asio::ip::tcp::resolver::iterator iterator = resolver.resolve(query);

		    boost::asio::ip::tcp::socket s(io_service_);
		    s.connect(*iterator);

		    return s.local_endpoint().address().to_string();
		}
		catch(const std::exception& e) {
			LogWarning("Exception: "<<e.what());
		}

		return "";
	}

	inline bool get_authorized() const
	{
		boost::unique_lock<boost::mutex> lock(mutex_);

		if (auth_attempt_time_ && ((time(0) - auth_attempt_time_) < 10))
			return !authorization_error_ && !authorization_.empty();

		if(authorization_.empty() || authorization_error_) {

			authorization_.clear();
			ip_ = get_my_ip();

			try {
				time(&auth_attempt_time_);

				const response& res( get(std::string("/auth/")+auth_hash_, false) );

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

	inline void init(const unsigned int retry = 3)
	{
		int attempts = retry;
		while(attempts-- && !quit) {
			if(get_authorized())
				return;

			boost::this_thread::sleep(boost::posix_time::seconds(10));
		}

		if (!quit)
			throw rml::exception("failed to get authorized with CoreAPI");
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

	coreapi(const std::string& base_url, const std::string& ui_version, boost::asio::io_service& io_service) :
		base_url_(base_url),
		auth_hash_(),
		ui_version_(ui_version),
		io_service_(io_service),
		client_(io_service_)
	{
	}

	/**
	 * logout from API
	 *
	 * @return true on loged-in & logout succeeded
	 */
	inline bool logout() const
	{
		boost::unique_lock<boost::mutex> lock(mutex_);

		if(!authorization_.empty() && !authorization_error_) {
			try {
				const response& res( delete_("/auth/", true, headers(), authorization_) );
				const bool& ok(res.first >= 200 && res.first < 300);
				if(ok)
					authorization_.clear();
				return ok;
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
	 * @param authorization
	 *
	 * @return pair< status, body >
	 */
	response get(const std::string& uri, const bool use_authorization = true, const headers& h = headers(), const std::string& authorization = std::string(), const std::string& ip = std::string()) const
	{
		int attempts = 2;

		const std::string& url(base_url_+uri);
		LogDebug(boost::this_thread::get_id()<<" url: "<<url);

		while(attempts--) {
			http_client_request request(url);
			if(use_authorization) {
				if(authorization.empty()) {
					if(true != get_authorized())
						throw rml::exception("Not authorized; invalid call sequence");
	
					request << boost::network::header("Authorization", authorization_);
					LogDebug(boost::this_thread::get_id()<<" Authorization: "<<authorization_);
				}
				else {
					request << boost::network::header("Authorization", authorization);
					LogDebug(boost::this_thread::get_id()<<" Authorization: "<<authorization);
				}
			}

			request << boost::network::header("UIVERSION", ui_version_);
			if (!ip.empty())
				request << boost::network::header("IP", ip);
			else if (!ip_.empty())
				request << boost::network::header("IP", ip_);

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

			if(use_authorization && authorization.empty() && ( status == 401 || status == 403 )) {
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
	 * @param h headers
	 * @param authorization
	 *
	 * @return response
	 */
	inline response get(const std::string& uri, const response::first_type& expected_status, const bool use_authorization = true, const headers& h = headers(), const std::string& authorization = std::string(), const std::string& ip = std::string()) const
	{
		const response& r(get(uri, use_authorization, h, authorization));
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
	 * @param h headers
	 * @param authorization
	 *
	 * @return pair< status, body >
	 */
	response delete_(const std::string& uri, const bool use_authorization = true, const headers& h = headers(), const std::string& authorization = std::string(), const std::string& ip = std::string()) const
	{
		int attempts = 2;

		const std::string& url(base_url_+uri);
		LogDebug(boost::this_thread::get_id()<<" url: "<<url);

		while(attempts--) {
			http_client_request request(url);
			if(use_authorization) {
				if(authorization.empty()) {
					if(true != get_authorized())
						throw rml::exception("Not authorized; invalid call sequence");
	
					request << boost::network::header("Authorization", authorization_);
					LogDebug(boost::this_thread::get_id()<<" Authorization: "<<authorization_);
				}
				else {
					request << boost::network::header("Authorization", authorization);
					LogDebug(boost::this_thread::get_id()<<" Authorization: "<<authorization);
				}
			}

			request << boost::network::header("UIVERSION", ui_version_);
			if (!ip.empty())
				request << boost::network::header("IP", ip);
			else if (!ip_.empty())
				request << boost::network::header("IP", ip_);

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

			if(use_authorization && authorization.empty() && ( status == 401 || status == 403 )) {
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
	 * @param h headers
	 * @param authorization
	 *
	 * @return pair< status, body >
	 */
	response post(const std::string& uri, const std::string& body, const std::string& content_type = "application/xml", const bool use_authorization = true, const headers& h = headers(), const std::string& authorization = std::string(), const std::string& ip = std::string()) const
	{
		int attempts = 2;

		const std::string& url(base_url_+uri);
		LogDebug(boost::this_thread::get_id()<<" url: "<<url);

		while(attempts--) {
			http_client_request request(url);
			if(use_authorization) {
				if(authorization.empty()) {
					if(true != get_authorized())
						throw rml::exception("Not authorized; invalid call sequence");
	
					request << boost::network::header("Authorization", authorization_);
					LogDebug(boost::this_thread::get_id()<<" Authorization: "<<authorization_);
				}
				else {
					request << boost::network::header("Authorization", authorization);
					LogDebug(boost::this_thread::get_id()<<" Authorization: "<<authorization);
				}
			}

			request << boost::network::header("UIVERSION", ui_version_);
			if (!ip.empty())
				request << boost::network::header("IP", ip);
			else if (!ip_.empty())
				request << boost::network::header("IP", ip_);

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

			if(use_authorization && authorization.empty() && ( status == 401 || status == 403 )) {
				authorization_error_ = true;
				continue;
			}

			return std::make_pair<uint16_t, std::string>(status, response.body());
		}

		throw rml::exception("failed after retry");
	}

	response put(const std::string& uri, const std::string& body, const std::string& content_type = "application/xml", const bool use_authorization = true, const headers& h = headers(), const std::string& authorization = std::string(), const std::string& ip = std::string()) const
	{
		int attempts = 2;

		const std::string& url(base_url_+uri);
		LogDebug(boost::this_thread::get_id()<<" url: "<<url);

		while(attempts--) {
			http_client_request request(url);
			if(use_authorization) {
				if(authorization.empty()) {
					if(true != get_authorized())
						throw rml::exception("Not authorized; invalid call sequence");

					request << boost::network::header("Authorization", authorization_);
					LogDebug(boost::this_thread::get_id()<<" Authorization: "<<authorization_);
				}
				else {
					request << boost::network::header("Authorization", authorization);
					LogDebug(boost::this_thread::get_id()<<" Authorization: "<<authorization);
				}
			}

			request << boost::network::header("UIVERSION", ui_version_);
			if (!ip.empty())
				request << boost::network::header("IP", ip);
			else if (!ip_.empty())
				request << boost::network::header("IP", ip_);

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
				response = client_.put(request);
			}
			catch(const std::exception& e) {
				LogError(boost::this_thread::get_id()<<" Error while performing POST; err:"<<e.what());
				throw;
			}

			int status = response.status();

			LogDebug(boost::this_thread::get_id()<<" response: "<<status<<' '<<response.status_message()<<'\n'<<response.body());

			if(use_authorization && authorization.empty() && ( status == 401 || status == 403 )) {
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
std::string coreapi< Tag >::ip_;

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
