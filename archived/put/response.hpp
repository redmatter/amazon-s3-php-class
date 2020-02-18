//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 12/02/2020.
//

#ifndef ARCHIVED_PUT_RESPONSE_HPP
#define ARCHIVED_PUT_RESPONSE_HPP

#include "../../common/archived/put_response/code.hpp"
#include "../../common/types.hpp"

#include <boost/chrono/chrono.hpp>
#include <boost/optional.hpp>

#include <ctime>

namespace archiving {
namespace archived {

namespace detail {

template<typename Clock>
inline boost::chrono::system_clock::time_point
to_system_clock(const boost::chrono::time_point<Clock>& t)
{
	return boost::chrono::system_clock::now() + (t - Clock::now());
}

template<>
inline boost::chrono::system_clock::time_point
to_system_clock<boost::chrono::system_clock>(const boost::chrono::time_point<boost::chrono::system_clock>& t)
{
	return t;
}

template<typename Clock>
inline std::time_t
to_time_t(const boost::chrono::time_point<Clock>& t)
{
	return boost::chrono::system_clock::to_time_t(to_system_clock(t));
}

} // namespace detail

namespace put {

using archived::detail::to_time_t;

namespace response_code = common::archived::put_response;

struct response
{
	typedef common::archived::put_response::code_t code_t;

	archiving::file_type::file_type_t file_type;
	archiving::org_id_t org_id;
	std::string uuid;

	code_t code;
	std::string message;
	std::time_t retry_after;

	response() : file_type(archiving::file_type::unknown), org_id(0), code(response_code::unknown), retry_after(0) {}

	response(archiving::file_type::file_type_t file_type,
			 archiving::org_id_t org_id,
			 const std::string& uuid,
			 code_t code,
			 const std::string& message = "",
			 std::time_t retry_after = 0)
		: file_type(file_type), org_id(org_id), uuid(uuid), code(code), message(message), retry_after(retry_after)
	{}

	explicit response(code_t code, const std::string& message = "", std::time_t retry_after = 0)
		: file_type(archiving::file_type::unknown), org_id(0), code(code), message(message), retry_after(retry_after)
	{}
};

std::ostream&
operator<<(std::ostream& os, const response& r)
{
	switch (r.code) {
		case response_code::continue_:
			return os << "OK PUT " << r.file_type << ' ' << r.org_id << ' ' << r.uuid;
		case response_code::accepted:
			return os << "OK RECV " << r.file_type << ' ' << r.org_id << ' ' << r.uuid;

		case response_code::bad_request:
		case response_code::internal_error:
		case response_code::server_busy:
		case response_code::insufficient_storage:
			if (r.uuid.empty()) {
				os << "ERR PUT <unknown> ";
			} else {
				os << "ERR PUT " << r.file_type << ' ' << r.org_id << ' ' << r.uuid << ' ';
			}

			os << "[ " << r.code;
			if (r.retry_after) {
				os << " retry-after:" << r.retry_after;
			}

			return os << " ] " << r.message;

		default:
			// nothing will be inserted into the stream as we don't know why we are here!
			return os;
	}
}

inline response
continue_(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return response(file_type, org_id, uuid, response_code::continue_);
}

inline response
accepted(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return response(file_type, org_id, uuid, response_code::accepted);
}

inline response
bad_request(archiving::file_type::file_type_t file_type,
			archiving::org_id_t org_id,
			const std::string& uuid,
			const std::string& message)
{
	return response(file_type, org_id, uuid, response_code::bad_request, message);
}

inline response
bad_request(const std::string& message)
{
	return response(response_code::bad_request, message);
}

inline response
internal_error(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return response(file_type, org_id, uuid, response_code::internal_error, "internal server error");
}

inline response
internal_error(const std::string& message)
{
	return response(response_code::internal_error, message);
}

inline response
internal_error(archiving::file_type::file_type_t file_type,
			   archiving::org_id_t org_id,
			   const std::string& uuid,
			   const std::string& message)
{
	return response(file_type, org_id, uuid, response_code::internal_error, message);
}

inline response
server_busy(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return response(file_type, org_id, uuid, response_code::server_busy, "server busy");
}

template<typename Clock>
inline response
server_busy(archiving::file_type::file_type_t file_type,
			archiving::org_id_t org_id,
			const std::string& uuid,
			const boost::chrono::time_point<Clock>& retry_after)
{
	return response(file_type, org_id, uuid, response_code::server_busy, "server busy", to_time_t(retry_after));
}

inline response
server_busy(archiving::file_type::file_type_t file_type,
			archiving::org_id_t org_id,
			const std::string& uuid,
			const std::time_t& retry_after)
{
	return response(file_type, org_id, uuid, response_code::server_busy, "server busy", retry_after);
}

inline response
insufficient_storage(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return response(file_type, org_id, uuid, response_code::insufficient_storage, "not enough space on disk");
}

} // namespace put
} // namespace archived
} // namespace archiving
#endif // ARCHIVED_PUT_RESPONSE_HPP
