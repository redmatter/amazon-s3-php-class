/*
 * get_server.hpp
 *
 *  Created on: 2 Feb 2011
 *      Author: dkorah
 */

#ifndef GET_SERVER_HPP_
#define GET_SERVER_HPP_

#include "retention_record.hpp"
#include "storage_endpoint.hpp"
#include "storage_backend.hpp"
#include "crypto.hpp"
#include "converter.hpp"
#include "coreapi_user.hpp"

#include "../common/coreapi.hpp"

#include <rml/logger.h>
#include <rml/exception.h>
#include <rml/utils.h>

#include <fcgiapp.h>

#include <pugixml/pugixml.hpp>

#include <boost/foreach.hpp>
#include <boost/program_options.hpp>
#include <boost/thread.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/erase.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <string>

#include <time.h>

namespace {

inline const char* get_xpath_single_value(pugi::xml_node& doc, const std::string& xpath, const char* default_value)
{
	RML_DEBUG( LogDebug("xpath: "<<xpath<<" default_value:"<<default_value) );
	pugi::xpath_node xpath_node = doc.select_single_node(xpath.c_str());
	if(xpath_node) {
		RML_DEBUG(LogDebug(xpath<<'='<<xpath_node.node().value()));
		return xpath_node.node().value();
	}

	RML_DEBUG(
		if (default_value)
			LogDebug(xpath<<'='<<default_value);
		else
			LogDebug(xpath<<"=NULL");
	);

	return default_value;
}

inline std::string get_xpath_single_value(pugi::xml_node& doc, const std::string& xpath, const std::string& default_value = "")
{
	RML_DEBUG(LogDebug("xpath: "<<xpath<<" default_value:"<<default_value));
	return get_xpath_single_value(doc, xpath, default_value.c_str());
}

inline unsigned long long get_xpath_single_value(pugi::xml_node& doc, const std::string& xpath, const unsigned long long& default_value = 0ULL)
{
	RML_DEBUG(LogDebug("xpath: "<<xpath<<" default_value:"<<default_value));
	return rml::utils::strtoull(get_xpath_single_value(doc, xpath, (const char*)0), default_value);
}

inline long long get_xpath_single_value(pugi::xml_node& doc, const std::string& xpath, const long long default_value = 0LL)
{
	RML_DEBUG(LogDebug("xpath: "<<xpath<<" default_value:"<<default_value));
	return rml::utils::strtoll(get_xpath_single_value(doc, xpath, (const char*)0), default_value);
}

}

namespace app {

class http_exception : public rml::exception
{
public:
	const uint16_t code;
	const std::string body;
	
	http_exception(const uint16_t code = 500, const std::string& message = std::string(), const std::string& body = std::string()) :
		rml::exception(message), code(code), body(body)
	{
	}
	
	virtual ~http_exception() throw ()
	{
	}
	
	static const char* get_status(const http_exception& he)
	{
		switch(he.code)
		{
		case 100: return "Continue"; break;
		case 101: return "Switching Protocols"; break;
		case 102: return "Processing"; break;
		case 200: return "OK"; break;
		case 201: return "Created"; break;
		case 202: return "Accepted"; break;
		case 203: return "Non-Authoritative Information"; break;
		case 204: return "No Content"; break;
		case 205: return "Reset Content"; break;
		case 206: return "Partial Content"; break;
		case 207: return "Multi-Status"; break;
		case 208: return "Already Reported"; break;
		case 226: return "IM Used"; break;
		case 300: return "Multiple Choices"; break;
		case 301: return "Moved Permanently"; break;
		case 302: return "Found"; break;
		case 303: return "See Other"; break;
		case 304: return "Not Modified"; break;
		case 305: return "Use Proxy"; break;
		case 306: return "Reserved"; break;
		case 307: return "Temporary Redirect"; break;
		case 400: return "Bad Request"; break;
		case 401: return "Unauthorized"; break;
		case 402: return "Payment Required"; break;
		case 403: return "Forbidden"; break;
		case 404: return "Not Found"; break;
		case 405: return "Method Not Allowed"; break;
		case 406: return "Not Acceptable"; break;
		case 407: return "Proxy Authentication Required"; break;
		case 408: return "Request Timeout"; break;
		case 409: return "Conflict"; break;
		case 410: return "Gone"; break;
		case 411: return "Length Required"; break;
		case 412: return "Precondition Failed"; break;
		case 413: return "Request Entity Too Large"; break;
		case 414: return "Request-URI Too Long"; break;
		case 415: return "Unsupported Media Type"; break;
		case 416: return "Requested Range Not Satisfiable"; break;
		case 417: return "Expectation Failed"; break;
		case 422: return "Unprocessable Entity"; break;
		case 423: return "Locked"; break;
		case 424: return "Failed Dependency"; break;
		case 425: return "Reserved for WebDAV advanced collections expired proposal"; break;
		case 426: return "Upgrade Required"; break;
		default:
		case 500: return "Internal Server Error"; break;
		case 501: return "Not Implemented"; break;
		case 502: return "Bad Gateway"; break;
		case 503: return "Service Unavailable"; break;
		case 504: return "Gateway Timeout"; break;
		case 505: return "HTTP Version Not Supported"; break;
		case 506: return "Variant Also Negotiates (Experimental)"; break;
		case 507: return "Insufficient Storage"; break;
		case 508: return "Loop Detected"; break;
		case 510: return "Not Extended"; break;
		}
	}
};

//namespace impl {

//template <>
class get_server
{
	struct mode
	{
		enum mode_t
		{
			normal,
			debug
		};
	};
	
public:
	enum { min_handler_threads = 1 };
	
	typedef boost::shared_ptr<get_server> pointer;

	struct http_method
	{
		enum http_method_t
		{
			unknown,
			get,
			head,
			post
		};
		
		static inline http_method_t from_string(const std::string& str)
		{
			if(str == "GET" || str == "get")
				return get;
			
			if(str == "HEAD" || str == "head")
				return head;
			
			if(str == "POST" || str == "post")
				return post;
			
			return unknown;
		}
	};
	
	struct request
	{
		typedef boost::shared_ptr<request> pointer;
		
		http_method::http_method_t method;
		std::string authorization;
		std::string remote_ip;
		uint64_t audit_token;
		
		operation_data::pointer opdata;
		
		FCGX_Request fcgi_req;
		mode::mode_t mode;
		boost::posix_time::ptime start_time;

		request() : audit_token(0)
		{
		}

		request(const int& fd_listen) : audit_token(0)
		{
			if(0 != FCGX_InitRequest(&fcgi_req, fd_listen, FCGI_FAIL_ACCEPT_ON_INTR))
				throw rml::exception("Failed to initialise fcgi request");			
		}
		
		~request()
		{
			if(opdata)
				opdata->cleanup();
		}
		
		bool accept(const mode::mode_t conf_mode)
		{
			int err = FCGX_Accept_r(&fcgi_req);
			if(err) {
				LogWarning("FCGX_Accept_r failed - err:"<<err<<" strerror:"<<strerror(errno));
				return false;
			}
			
			start_time = boost::posix_time::microsec_clock::local_time();
			mode = conf_mode;
			method = http_method::from_string(FCGX_GetParam("REQUEST_METHOD", fcgi_req.envp));
			
			const boost::thread::id& thread_id = boost::this_thread::get_id();

			const char* qry_str = FCGX_GetParam("QUERY_STRING", fcgi_req.envp);
			if(qry_str) {
				LogMessage(thread_id<<':'<<"Got request; query-string: " << qry_str);
				std::vector<std::string> qry_str_pieces;
				boost::split(qry_str_pieces, qry_str, boost::algorithm::is_any_of("&?"));
				if(!qry_str_pieces.empty()) {
					BOOST_FOREACH(std::string qry_str_piece, qry_str_pieces) {
						if(boost::starts_with(qry_str_piece, "authorization="))
							authorization = std::string(qry_str_piece, 14);
						if(boost::starts_with(qry_str_piece, "remote-ip="))
							remote_ip = std::string(qry_str_piece, 10);
						else if(boost::starts_with(qry_str_piece, "audit-token=")) {
							std::string token(qry_str_piece, 12);
							audit_token = rml::utils::strtoull(&token[0], 0ULL);
						}
						else if(boost::starts_with(qry_str_piece, "debug="))
							mode = (conf_mode == mode::debug || '1' == qry_str_piece[6])? mode::debug : mode::normal;
					}
				}
			}
			
			if(!audit_token)
				throw http_exception(400, "Audit token missing");			
			
			return true;
		}
		
		void respond(const http_exception& e)
		{
			if(e.body.empty())
				FCGX_FPrintF(fcgi_req.out,
						"Status: %d %s\r\n"
						"Content-type: application/xml\r\n"
						"\r\n"
						"<archived>\n"
						"    <error code=\"%d\">%s</error>\n"
						"</archived>", 
						e.code, http_exception::get_status(e), e.code, e.what());
			else
				FCGX_FPrintF(fcgi_req.out,
						"Status: %d %s\r\n"
						"Content-type: application/xml\r\n"
						"\r\n"
						"%s",
						e.code, http_exception::get_status(e), e.body.c_str());
			
			finish();
		}
		
		void respond()
		{
			rml::utils::tmp_fstream::pointer fs = opdata->get_tmp_fstream();
			size_t content_length = fs->get_size();
			
			FCGX_FPrintF(fcgi_req.out, "Status: 200 OK\r\n"
					"Pragma: public\r\n"
					"Expires: 0\r\n"
					"Cache-Control: must-revalidate, post-check=0, pre-check=0\r\n"
					"Cache-Control: private\r\n"
					"Content-Type: application/octet-stream\r\n"
					"Content-Disposition: attachment; filename=\"%s.%s\";\r\n"
					"Content-Transfer-Encoding: binary\r\n"
					"Content-Length: %d\r\n"
					"AuditLogToken: %d\r\n"
					"\r\n",
					opdata->p_detail->identifier.uuid.c_str(), types::file_format::to_extension(opdata->p_detail->current_file_format), content_length, audit_token);
			
			fs->re_open();
			while(*fs) {
				char buf[1025];
				fs->read(buf, 1024);
				FCGX_PutStr(buf, fs->gcount(), fcgi_req.out);
			}
			
			finish();
		}
		
		void respond(const storage::string_map_t& props)
		{
			FCGX_FPrintF(fcgi_req.out, "Status: 200 OK\r\n");
			
			{
				storage::string_map_t::const_iterator itr = props.find("content-type");
				if(itr == props.end())
					FCGX_FPrintF(fcgi_req.out, "Content-type: %s\r\n", itr->second.c_str());
			}
			
			{
				storage::string_map_t::const_iterator itr = props.find("content-length");
				if(itr == props.end())
					FCGX_FPrintF(fcgi_req.out, "Content-length: %s\r\n", itr->second.c_str());
			}
					
			FCGX_FPrintF(fcgi_req.out, "\r\n");
			
			for(storage::string_map_t::const_iterator itr = props.begin(); itr != props.end(); itr++) {
				FCGX_FPrintF(fcgi_req.out, "%s: %s\n", itr->first.c_str(), itr->second.c_str());
			}
			
			finish();
		}
		
		void finish()
		{
			FCGX_Finish_r(&fcgi_req);
		}
	};

	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("get_server.fcgi_listen_socket", boost::program_options::value<std::string>()->default_value(":10101"), "GET server fcgi listen port")
				("get_server.fcgi_listen_queue_depth", boost::program_options::value<unsigned int>()->default_value(100), "GET server fcgi listen queue")
				("get_server.handler_threads", boost::program_options::value<unsigned int>()->default_value(2), "GET server threads")
				("get_server.debug", boost::program_options::value<std::string>()->default_value("false"), "GET server fcgi debug enabled?")
				
				;
	}
	
	get_server(const boost::program_options::variables_map& confMap, const int& fd_listen, const app::coreapi& api,
			const retention_record::cached_finder_t& retention_record_cached_finder, storage::cached_endpoint_finder_t& storage_endpoint_finder,
			const policy::cached_finder_t& cached_policy_finder, const coreapi_user::cached_finder_t& coreapi_user_finder, volatile bool& quit) :
		fd_listen_(fd_listen), mode_(confMap["get_server.debug"].as<std::string>() == "true"? mode::debug: mode::normal),
		confMap_(confMap), api_(api), retention_record_cached_finder_(retention_record_cached_finder), 
		storage_endpoint_finder_(storage_endpoint_finder), cached_policy_finder_(cached_policy_finder),
		coreapi_user_finder_(coreapi_user_finder), quit_(quit)
	{
	}
	
	void run()
	{
		const boost::thread::id& thread_id = boost::this_thread::get_id();
				
#define Debug(line) \
		LogIf(req->mode == mode::debug, req->fcgi_req.requestId<<':'<<thread_id<<": "<<line);

#define Timing(desc) \
		Debug("timing - "<< boost::posix_time::to_simple_string(boost::posix_time::microsec_clock::local_time() - req->start_time ) << " - "<<desc)

		while (!quit_) {
			request::pointer req(new request(fd_listen_));
			
			try {
				const bool& accepted(req->accept(mode_));
				
				if (!accepted && quit_) {
					break;
				}
				else if(!accepted) {
					LogMessage("Accept failed; continuing with another attempt");
					sleep(1);
					continue;
				}
				
				archiving::archivable::identifier identifier;
				{
					std::vector<std::string> path;
					char* path_info;
					
					if(!(path_info = FCGX_GetParam("PATH_INFO", req->fcgi_req.envp)))
						if(!(path_info = FCGX_GetParam("SCRIPT_NAME", req->fcgi_req.envp)))
							throw http_exception(500, "Variables missing in environment");

					Debug("path_info: "<<path_info);
					boost::split(path, path_info, boost::algorithm::is_any_of("/"));
					if(path.size() != 4)
						throw http_exception(404, "Invalid URL");
					
					boost::algorithm::erase_all(path[3], "-");
					path[3] = std::string(path[3], 0, 8)+'-'+std::string(path[3], 8, 4)+'-'+std::string(path[3], 12, 4)+
							'-'+std::string(path[3], 16, 4)+'-'+std::string(path[3], 20);

					identifier = archiving::archivable::identifier(archiving::archivable::file_type::from_string(path[1]),
							archiving::org_id_t(rml::utils::strtoull(path[2].c_str(), 0ULL)), path[3]);
				}
				
				Timing("Starting GET for "<<identifier);
				
				retention_record::pointer retention = retention_record_cached_finder_(identifier);
				if(!retention) {
					retention_record_cached_finder_.forget(identifier);
					throw http_exception(404, "Retention record not found");
				}
				Timing("Retention record found");
				
				app::policy::pointer policy;
				{
					const std::pair< archiving::org_id_t, app::policy::id_type >& lookup(
							std::make_pair< archiving::org_id_t, app::policy::id_type >(retention->identifier.org, retention->policy_id));
					policy = cached_policy_finder_(lookup);
					if(!policy) {
						cached_policy_finder_.forget(lookup);
						LogWarning("Policy (from retention) lookup failed; identifier:"<<retention->identifier<<" lookup:"<<lookup.first<<'/'<<lookup.second);
						throw http_exception(500);
					}
				}
				
				archiving::user_id_t user_id(0);
				archiving::org_id_t user_org_id;

				if(!req->authorization.empty())
				{
					coreapi_user::pointer p_user;
					{
						const coreapi_user::lookup& param( std::make_pair(req->authorization, req->remote_ip) );
						if (!(p_user = coreapi_user_finder_(param))) {
							coreapi_user_finder_.forget(param);
							throw http_exception(403, "Failed to validate auth token");
						}
					}

					user_id = p_user->id;
					user_org_id = p_user->org_id;
					
					Timing("User verified");
				}
				else {
					// it could be an unrestricted access; lets check the policy
					if(!policy->allow_unrestricted_access)
						throw http_exception(401, "Authorization not given");
					
					LogMessage("Allowing policy based unrestricted access for "<<retention->identifier<<" policy:"<<retention->policy_id);
					user_org_id = retention->identifier.org;
				}
				
				if(retention->purged || retention->expired) {
					Debug((retention->purged? "object found purged": "object found expired"));
					throw http_exception(410, "object found purged" /*shush... till we purge; no matter what!*/);
				}

				storage::basic_endpoint::pointer endpoint = storage_endpoint_finder_(std::make_pair< archiving::org_id_t, archiving::endpoint_id_t >(user_org_id, retention->endpoint_id));
				if(!endpoint) {
					storage_endpoint_finder_.forget(std::make_pair< archiving::org_id_t, archiving::endpoint_id_t >(user_org_id, retention->endpoint_id));
					throw http_exception(500, "Endpoint not found");
				}
				Timing("Endpoint looked up");
				
				storage::s3::backend::pointer storage_backend = storage::s3::backend::get_instance();

				operation_data::detail::pointer detail(new operation_data::detail());
				detail->identifier = identifier;
				detail->p_endpoint = endpoint;
				
				switch(req->method) {
				case http_method::get:
				{
					detail->arrival_time = time(0);
					detail->current_file_format = retention->file_format;
					detail->target_file_format = types::file_format::make(false, false, retention->file_format); // XXX fudge; we need to read filetype!
					/*
					// implemented in API using a lame based stream filter... dont need this anymore
					if (policy->disallow_recording_download && (detail->target_file_format == types::file_format::clear_wav))
						detail->target_file_format = types::file_format::clear_mp3;
					*/

					detail->state = operation_state::get_file_retrieve;

					req->opdata = app::operation_data::pointer(new operation_data(detail));
					
					try {
						storage_backend->get(req->opdata);
						Timing("File retrieved from storage");
						
						app::crypto crypto;
						app::converter::convert(req->opdata, crypto);
						Timing("File converted");
						
						req->respond();
						Timing("done");
					}
					catch(const std::exception& e) {
						throw http_exception(404, e.what());
					}
					
					break;
				}
				
				case http_method::head:
				{
					try {
						req->opdata = app::operation_data::pointer(new operation_data(detail));
						
						storage::string_map_t props;
						storage_backend->stat(req->opdata, props);
						Timing("Stat from storage");
						req->respond(props);
						Timing("done");
					}
					catch(const std::exception& e) {
						throw http_exception(404, e.what());
					}
					
					break;
				}

				default:
					throw http_exception(405);
				}
			}
			catch(const http_exception& e) {
				Debug("HTTP Exception: "<<e.what());
				req->respond(e);
			}
			catch(const std::exception& e) {
				Debug("Exception: "<<e.what());
				req->respond(http_exception(500, e.what()));
			}
		}
		
#undef Timing
#undef Debug
		
	}
	
private:
	const int fd_listen_;
	const mode::mode_t mode_;
	const boost::program_options::variables_map& confMap_;
	app::coreapi api_;
	const retention_record::cached_finder_t& retention_record_cached_finder_;
	const storage::cached_endpoint_finder_t& storage_endpoint_finder_;
	const policy::cached_finder_t& cached_policy_finder_;
	const coreapi_user::cached_finder_t& coreapi_user_finder_;
	
	volatile bool& quit_;
	
//	static boost::mutex mutex_;
};

//template <>
//boost::mutex get_server::mutex_;

//}  // namespace impl

//typedef impl::get_server<> get_server;

}  // namespace app

#endif /* GET_SERVER_HPP_ */
