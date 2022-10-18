//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 12/11/2012.
//

#ifndef FCGI_STORAGE_ACCESS_HPP_
#define FCGI_STORAGE_ACCESS_HPP_

#include "retention_record.hpp"
#include "storage_endpoint.hpp"
#include "storage/backends.hpp"
#include "crypto.hpp"
#include "converter.hpp"
#include "coreapi_user.hpp"
#include "fcgi_response.hpp"

#include <rml/fcgi/request.hpp>
#include <rml/coreapi.hpp>
#include <rml/fcgi/iostreams_sink.hpp>

#include <boost/bind.hpp>

#define Debug(line) \
		LogIf(p_request->debug_mode == rml::fcgi::debug::on, p_request->fcgi_req.requestId<<':'<<thread_id<<": "<<line);

#define Timing(desc) \
		Debug("[timing] "<< boost::posix_time::to_simple_string(boost::posix_time::microsec_clock::local_time() - p_request->start_time ) << " - "<<desc)

namespace app {

namespace fcgi {

class storage_access_handler : public rml::fcgi::request::handler
{
	const retention_record::cached_finder_t& retention_record_cached_finder_;
	const retention_record::fn_is_expired_t& retention_record_is_expired_;
	const policy::cached_finder_t& cached_policy_finder_;
	const coreapi_user::cached_finder_t& coreapi_user_finder_;
	const storage::cached_endpoint_finder_t& storage_endpoint_finder_;
	const coreapi_eventlog::cached_finder_t& eventlog_finder_;
    const archiving::archived::storage::backends& storage_backends_;

    boost::shared_ptr<archiving::archived::storage::basic_backend> storage_backend;
    boost::shared_ptr<storage::basic_endpoint> endpoint;
    archiving::archivable::identifier identifier;
    boost::shared_ptr<response::configurator> response_configurator;
    archiving::archived::storage::headers_list headers;

public:
	static inline rml::fcgi::request::handler::pointer make(
			const retention_record::cached_finder_t& retention_record_cached_finder,
			const retention_record::fn_is_expired_t& retention_record_is_expired,
			const policy::cached_finder_t& cached_policy_finder,
			const coreapi_user::cached_finder_t& coreapi_user_finder,
			const storage::cached_endpoint_finder_t& storage_endpoint_finder,
			const coreapi_eventlog::cached_finder_t& eventlog_finder,
            const archiving::archived::storage::backends& storage_backends,
			rml::fcgi::request::pointer p_request, const std::string& routed_path)
	{
        return boost::shared_ptr<rml::fcgi::request::handler>(new storage_access_handler(
            retention_record_cached_finder, retention_record_is_expired, cached_policy_finder, coreapi_user_finder,
            storage_endpoint_finder, eventlog_finder, storage_backends, p_request, routed_path));
    }

	storage_access_handler(
			const retention_record::cached_finder_t& retention_record_cached_finder,
			const retention_record::fn_is_expired_t& retention_record_is_expired,
			const policy::cached_finder_t& cached_policy_finder,
			const coreapi_user::cached_finder_t& coreapi_user_finder,
			const storage::cached_endpoint_finder_t& storage_endpoint_finder,
			const coreapi_eventlog::cached_finder_t& eventlog_finder,
            const archiving::archived::storage::backends& storage_backends,
            const rml::fcgi::request::pointer& p_request, const std::string& routed_path) :

			rml::fcgi::request::handler(p_request, routed_path),

			retention_record_cached_finder_(retention_record_cached_finder),
			retention_record_is_expired_(retention_record_is_expired),
			cached_policy_finder_(cached_policy_finder),
			coreapi_user_finder_(coreapi_user_finder),
			storage_endpoint_finder_(storage_endpoint_finder),
			eventlog_finder_(eventlog_finder),
            storage_backends_(storage_backends)
	{
		LogDebug("New storage-access request: "<<rml::fcgi::http_method::to_string(p_request->method)<<' '<<std::string(p_request->pathinfo));
	}

    // Handle request here; no need to catch and convert exceptions into http_exceptions. That is already taken care of!
	virtual void operator()()
	{
		const boost::thread::id& thread_id = boost::this_thread::get_id();

		if(p_request->method != rml::fcgi::http_method::get && p_request->method != rml::fcgi::http_method::head)
			throw rml::fcgi::http_exception(405);

		char* authprization_header = FCGX_GetParam("HTTP_AUTHORIZATION", p_request->fcgi_req.envp);
		char* ip_header = FCGX_GetParam("HTTP_IP", p_request->fcgi_req.envp);
		const std::string& authorization((authprization_header && authprization_header[0])? authprization_header: p_request->querystring.get_first("authorization"));
		const std::string& remote_ip((ip_header && ip_header[0])? ip_header: p_request->querystring.get_first("remote-ip"));

		{
			std::string uuid(p_request->pathinfo.at< std::string >(2));
			if(uuid.rfind('-') == std::string::npos) {
				// boost::algorithm::erase_all(uuid, "-");
				uuid = std::string(uuid, 0, 8)+'-'+std::string(uuid, 8, 4)+'-'+std::string(uuid, 12, 4)+'-'+std::string(uuid, 16, 4)+'-'+std::string(uuid, 20);
			}

			identifier = archiving::archivable::identifier(
					archiving::file_type::from_string(p_request->pathinfo.at< std::string >(0)),
					p_request->pathinfo.at< archiving::org_id_t >(1), uuid);
		}

		std::string this_op;
		{
			std::ostringstream ostr;
			ostr << rml::fcgi::http_method::to_string(p_request->method)<<' '<<identifier<<" - ";
			this_op = ostr.str();
		}

		coreapi_eventlog::id_t audit_token = get_audit_token(this_op);

		Timing(this_op<<"request verified");

		retention_record::pointer retention = retention_record_cached_finder_(identifier);
		if(!retention) {
			retention_record_cached_finder_.forget(identifier);
			throw rml::fcgi::http_exception(404, "Retention record not found");
		}

		Timing(this_op<<"retention record found");

		app::policy::pointer policy;
		{
			const std::pair< archiving::org_id_t, app::policy::id_type >& lookup(
					std::make_pair< archiving::org_id_t, app::policy::id_type >(retention->identifier.org, retention->policy_id));
			policy = cached_policy_finder_(lookup);
			if(!policy) {
				cached_policy_finder_.forget(lookup);
				LogWarning(this_op<<"policy (from retention) lookup failed; lookup:"<<lookup.first<<'/'<<lookup.second);
				throw rml::fcgi::http_exception(500);
			}
		}

		archiving::user_id_t user_id(0);
		archiving::org_id_t user_org_id;
		types::file_format::file_format_t target_file_format(types::file_format::make(false, false, retention->file_format));

		if(!authorization.empty())
		{
			coreapi_user::pointer p_user;
			{
				const coreapi_user::lookup& param( std::make_pair(authorization, remote_ip) );
				if (!(p_user = coreapi_user_finder_(param))) {
					coreapi_user_finder_.forget(param);
					throw rml::fcgi::http_exception(403, "Failed to validate auth token");
				}
			}

			// if we are dealing with a system user, it is assumed that the client has the means to decrypt and decompress the file.
			// it is the only way we can protect against exploits via the hole we have created for accessing call-buffering
			if(p_user->is_system_user && p_user->id < 0) {
				user_id = p_user->id;
				user_org_id = retention->identifier.org;
				target_file_format = retention->file_format;

				Timing(this_op<<"user verified; ID:"<<p_user->org_id<<'/'<<user_id<<" (sudo-org:"<<user_org_id<<')');
			}
			else {
				user_id = p_user->id;
				user_org_id = p_user->org_id;

				Timing(this_op<<"user verified; ID:"<<user_org_id<<'/'<<user_id);
			}
		}
		else {
			// it could be an unrestricted access; lets check the policy
			if(!policy->allow_unrestricted_access())
				throw rml::fcgi::http_exception(401, "Authorization not given");

			LogMessage(this_op<<"allowing policy based unrestricted access; policy:"<<retention->policy_id);
			user_org_id = retention->identifier.org;
		}

		if(retention->purged || retention_record_is_expired_(retention)) {
			Debug(this_op<<(retention->purged? "object found purged": "object found expired"));
			throw rml::fcgi::http_exception(410, "object found purged" /*shush... till we purge; no matter what!*/);
		}

		endpoint = storage_endpoint_finder_(
				std::make_pair< archiving::org_id_t, archiving::endpoint_id_t >(
						user_org_id, retention->endpoint_id));
		if(!endpoint) {
			storage_endpoint_finder_.forget(std::make_pair< archiving::org_id_t, archiving::endpoint_id_t >(
					user_org_id, retention->endpoint_id));
			throw rml::fcgi::http_exception(500, "Endpoint not found");
		}

		storage_backend = storage_backends_.get(endpoint);

		Timing(this_op<<"endpoint looked up");

        switch (p_request->method) {
        case rml::fcgi::http_method::get:
            response_configurator = boost::make_shared<response::configurator>(p_request, endpoint->encryption_key,
                                                                               target_file_format, audit_token);
            Timing(this_op << "fetch-convert-and-respond configured");
            break;

        case rml::fcgi::http_method::head:
            uint64_t content_length;
            bool not_found;
            if (!storage_backend->head(endpoint, identifier, headers, content_length, not_found)) {
                throw rml::fcgi::http_exception(404);
            }

            Timing(this_op << "HEAD response configured");
            break;

        default:
            throw rml::fcgi::http_exception(405);
        }
    }

    coreapi_eventlog::id_t get_audit_token(const std::string& this_op) const
    {
        coreapi_eventlog::id_t audit_token = p_request->querystring.get_first_as<coreapi_eventlog::id_t>("audit-token");
        if (!audit_token) {
            LogMessage(this_op << "No audit-token was provided");
            throw rml::fcgi::http_exception(400, "Request not audited");
        }

        const std::pair<archiving::org_id_t, coreapi_eventlog::id_t>& lookup(
            std::make_pair(identifier.org, audit_token));
        coreapi_eventlog::pointer p_eventlog = eventlog_finder_(lookup);
        if (!p_eventlog) {
            eventlog_finder_.forget(lookup);
            LogMessage(this_op << "The given audit-token could not be verified");
            throw rml::fcgi::http_exception(400, "Request not audited");
        }

        if ( // older than 60 minute or is not Archiving::FetchObject or does not have the identifier in message
            p_eventlog->time < (time(0) - 3600) || p_eventlog->context != "Archiving" ||
            p_eventlog->action != "FetchObject" ||
            p_eventlog->message.find(boost::str(boost::format(" %1% ") %
                                                archivable::identifier::to_string(identifier))) == std::string::npos) {
            audit_token = 0;
            LogMessage(this_op << "The given audit-token is invalid");
            throw rml::fcgi::http_exception(400, "Request not audited");
        }

        return audit_token;
    }

    virtual void respond() const
	{
        // If a HEAD request, respond based on the headers we obtained from storage
        if (p_request->method == rml::fcgi::http_method::head) {
            FCGX_FPrintF(p_request->fcgi_req.out,
                         "Status: 200 OK\r\n"
                         "Content-Type: %s\r\n"
                         "Content-Length: %d\r\n"
                         "\r\n",
                         data_sub_format::to_mime_type(headers.get("file-type").get_value_or("binary")),
                         headers.get("clear-size").get_value_or("0").data());
            return;
        }

        // If a GET request, stream the response
        storage_backend->get(endpoint, identifier, boost::bind(&response::configurator::configure, response_configurator, _1));
	}

	virtual void respond(const rml::fcgi::http_exception& e) const
	{
		if(e.body.empty())
			FCGX_FPrintF(p_request->fcgi_req.out,
					"Status: %d %s\r\n"
					"Content-type: application/xml\r\n"
					"\r\n"
					"<archived>\n"
					"    <error code=\"%d\">%s</error>\n"
					"</archived>",
					e.code, rml::fcgi::http_exception::get_status(e), e.code, e.what());
		else
			FCGX_FPrintF(p_request->fcgi_req.out,
					"Status: %d %s\r\n"
					"Content-type: application/xml\r\n"
					"\r\n"
					"%s",
					e.code, rml::fcgi::http_exception::get_status(e), e.body.c_str());
	}
};

}  // namespace fcgi

}  // namespace app

#undef Timing
#undef Debug

#endif /* FCGI_STORAGE_ACCESS_HPP_ */
