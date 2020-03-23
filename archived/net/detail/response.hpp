//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 19/02/2020.
//

#ifndef ARCHIVING_ARCHIVED_NET_DETAIL_RESPONSE_HPP
#define ARCHIVING_ARCHIVED_NET_DETAIL_RESPONSE_HPP

#include "../../../common/net/archived/response/code.hpp"
#include "../../../common/types.hpp"

#include <ctime>

namespace archiving {
namespace archived {
namespace net {
namespace detail {

namespace response_code = common::net::archived::response;

template <typename Verb>
struct response
{
	typedef Verb verb_t;
	typedef response_code::code_t code_t;

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

template <typename Verb>
std::ostream&
operator<<(std::ostream& os, const response<Verb>& r)
{
	typename response<Verb>::verb_t verb;

	switch (r.code) {
		case response_code::continue_:
		case response_code::accepted:
			return os << "OK " << verb << ' ' << r.file_type << ' ' << r.org_id << ' ' << r.uuid;

		case response_code::bad_request:
		case response_code::internal_error:
		case response_code::server_busy:
		case response_code::insufficient_storage:
			if (r.uuid.empty()) {
				os << "ERR " << verb << " <unknown> ";
			} else {
				os << "ERR " << verb << ' ' << r.file_type << ' ' << r.org_id << ' ' << r.uuid << ' ';
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

} // namespace detail
} // namespace net
} // namespace archived
} // namespace archiving
#endif // ARCHIVING_ARCHIVED_NET_DETAIL_RESPONSE_HPP
