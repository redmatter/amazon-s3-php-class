/*
 * fcgi_storage_access.hpp
 *
 *  Created on: 12/11/2012
 *      Author: dkorah
 */

#ifndef FCGI_STORAGE_ACCESS_HPP_
#define FCGI_STORAGE_ACCESS_HPP_

#include "retention_record.hpp"
#include "storage_endpoint.hpp"
#include "storage_backend.hpp"
#include "crypto.hpp"
#include "converter.hpp"
#include "coreapi_user.hpp"

#include "../common/coreapi.hpp"

#include <rml/fcgi/request.hpp>

#define Debug(line) \
		LogIf(p_request->debug_mode == rml::fcgi::debug::on, p_request->fcgi_req.requestId<<':'<<thread_id<<": "<<line);

#define Timing(desc) \
		Debug("timing - "<< boost::posix_time::to_simple_string(boost::posix_time::microsec_clock::local_time() - p_request->start_time ) << " - "<<desc)

namespace app {

namespace fcgi {

class storage_access_handler : public rml::fcgi::request::handler
{
	const retention_record::cached_finder_t& retention_record_cached_finder_;
	const policy::cached_finder_t& cached_policy_finder_;
	const coreapi_user::cached_finder_t& coreapi_user_finder_;
	const storage::cached_endpoint_finder_t& storage_endpoint_finder_;

	uint64_t audit_token;
	operation_data::pointer opdata;
	storage::string_map_t props;

public:
	static inline rml::fcgi::request::handler::pointer make(
			const retention_record::cached_finder_t& retention_record_cached_finder,
			const policy::cached_finder_t& cached_policy_finder,
			const coreapi_user::cached_finder_t& coreapi_user_finder,
			const storage::cached_endpoint_finder_t& storage_endpoint_finder,
			rml::fcgi::request::pointer p_request, const std::string& routed_path)
	{
		return boost::static_pointer_cast<rml::fcgi::request::handler>(
				boost::make_shared< storage_access_handler >(
						retention_record_cached_finder, cached_policy_finder, coreapi_user_finder, storage_endpoint_finder, p_request, routed_path));
	}

	explicit storage_access_handler(
			const retention_record::cached_finder_t& retention_record_cached_finder,
			const policy::cached_finder_t& cached_policy_finder,
			const coreapi_user::cached_finder_t& coreapi_user_finder,
			const storage::cached_endpoint_finder_t& storage_endpoint_finder,
			rml::fcgi::request::pointer p_request, const std::string& routed_path) :

			rml::fcgi::request::handler(p_request, routed_path),

			retention_record_cached_finder_(retention_record_cached_finder),
			cached_policy_finder_(cached_policy_finder),
			coreapi_user_finder_(coreapi_user_finder),
			storage_endpoint_finder_(storage_endpoint_finder),

			audit_token(0)
	{
		LogDebug("New storage-access request: "<<rml::fcgi::http_method::to_string(p_request->method)<<' '<<std::string(p_request->pathinfo));
	}

	virtual void operator()()
	{
		const boost::thread::id& thread_id = boost::this_thread::get_id();

		if(p_request->method != rml::fcgi::http_method::get && p_request->method != rml::fcgi::http_method::head)
			throw rml::fcgi::http_exception(405);

		char* authprization_header = FCGX_GetParam("HTTP_AUTHORIZATION", p_request->fcgi_req.envp);
		char* ip_header = FCGX_GetParam("HTTP_IP", p_request->fcgi_req.envp);
		const std::string& authorization((authprization_header && authprization_header[0])? authprization_header: p_request->querystring.get_first("authorization"));
		const std::string& remote_ip((ip_header && ip_header[0])? ip_header: p_request->querystring.get_first("remote-ip"));

		audit_token = p_request->querystring.get_first_as< uint64_t >("audit-token");

		archiving::archivable::identifier identifier;
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

			user_id = p_user->id;
			user_org_id = p_user->org_id;

			Timing(this_op<<"user verified; ID:"<<user_org_id<<'/'<<user_id);
		}
		else {
			// it could be an unrestricted access; lets check the policy
			if(!policy->allow_unrestricted_access)
				throw rml::fcgi::http_exception(401, "Authorization not given");

			LogMessage(this_op<<"allowing policy based unrestricted access; policy:"<<retention->policy_id);
			user_org_id = retention->identifier.org;
		}

		if(retention->purged || retention->expired) {
			Debug(this_op<<(retention->purged? "object found purged": "object found expired"));
			throw rml::fcgi::http_exception(410, "object found purged" /*shush... till we purge; no matter what!*/);
		}

		storage::basic_endpoint::pointer endpoint = storage_endpoint_finder_(
				std::make_pair< archiving::org_id_t, archiving::endpoint_id_t >(
						user_org_id, retention->endpoint_id));
		if(!endpoint) {
			storage_endpoint_finder_.forget(std::make_pair< archiving::org_id_t, archiving::endpoint_id_t >(
					user_org_id, retention->endpoint_id));
			throw rml::fcgi::http_exception(500, "Endpoint not found");
		}

		Timing(this_op<<"endpoint looked up");

		storage::s3::backend::pointer storage_backend = storage::s3::backend::get_instance();

		operation_data::detail::pointer detail(new operation_data::detail());
		detail->identifier = identifier;
		detail->p_endpoint = endpoint;

		// GET
		if(p_request->method == rml::fcgi::http_method::get) {
			detail->arrival_time = time(0);
			detail->current_file_format = retention->file_format;
			detail->target_file_format = types::file_format::make(false, false, retention->file_format); // XXX fudge; we need to read filetype!
			/*
			// implemented in API using a lame based stream filter... dont need this anymore
			if (policy->disallow_recording_download && (detail->target_file_format == types::file_format::clear_wav))
				detail->target_file_format = types::file_format::clear_mp3;
			*/

			detail->state = operation_state::get_file_retrieve;

			opdata = app::operation_data::pointer(new operation_data(detail));

			try {
				storage_backend->get(opdata);
				Timing(this_op<<"file retrieved from storage");

				app::crypto crypto;
				app::converter::convert(opdata, crypto);
				Timing(this_op<<"file converted");
			}
			catch(const std::exception& e) {
				throw rml::fcgi::http_exception(404, e.what());
			}
		}

		// HEAD
		else if(p_request->method == rml::fcgi::http_method::head) {
			try {
				opdata = app::operation_data::pointer(new operation_data(detail));

				storage_backend->stat(opdata, props);
				Timing(this_op<<"got metadata from storage");
			}
			catch(const std::exception& e) {
				throw rml::fcgi::http_exception(404, e.what());
			}
		}
	}

	virtual void respond() const
	{
		// GET
		if(p_request->method == rml::fcgi::http_method::get) {
			rml::utils::tmp_fstream::pointer fs = opdata->get_tmp_fstream();
			size_t content_length = fs->get_size();

			FCGX_FPrintF(p_request->fcgi_req.out, "Status: 200 OK\r\n"
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
				FCGX_PutStr(buf, fs->gcount(), p_request->fcgi_req.out);
			}
		}

		// HEAD
		else if(p_request->method == rml::fcgi::http_method::head) {
			FCGX_FPrintF(p_request->fcgi_req.out, "Status: 200 OK\r\n");

			{
				storage::string_map_t::const_iterator itr = props.find("content-type");
				if(itr == props.end())
					FCGX_FPrintF(p_request->fcgi_req.out, "Content-type: %s\r\n", itr->second.c_str());
			}

			{
				storage::string_map_t::const_iterator itr = props.find("content-length");
				if(itr == props.end())
					FCGX_FPrintF(p_request->fcgi_req.out, "Content-length: %s\r\n", itr->second.c_str());
			}

			for(storage::string_map_t::const_iterator itr = props.begin(); itr != props.end(); itr++)
				FCGX_FPrintF(p_request->fcgi_req.out, "%s: %s\r\n", itr->first.c_str(), itr->second.c_str());

			FCGX_FPrintF(p_request->fcgi_req.out, "\r\n");
		}
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


#endif /* FCGI_STORAGE_ACCESS_HPP_ */
