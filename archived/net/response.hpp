//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 12/02/2020.
//

#ifndef ARCHIVED_PUT_RESPONSE_HPP
#define ARCHIVED_PUT_RESPONSE_HPP

#include "detail/make_response.hpp"
#include "../../common/net/archived/verb.hpp"

namespace archiving {
namespace archived {
namespace net {
namespace put {

using common::net::archived::verb::PUT;
using detail::response;

inline response<PUT>
continue_(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return detail::continue_<PUT>(file_type, org_id, uuid);
}

inline response<PUT>
bad_request(archiving::file_type::file_type_t file_type,
			archiving::org_id_t org_id,
			const std::string& uuid,
			const std::string& message)
{
	return detail::bad_request<PUT>(file_type, org_id, uuid, message);
}

inline response<PUT>
bad_request(const std::string& message)
{
	return detail::bad_request<PUT>(message);
}

inline response<PUT>
internal_error(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return detail::internal_error<PUT>(file_type, org_id, uuid);
}

inline response<PUT>
internal_error(const std::string& message)
{
	return detail::internal_error<PUT>(message);
}

inline response<PUT>
internal_error(archiving::file_type::file_type_t file_type,
			   archiving::org_id_t org_id,
			   const std::string& uuid,
			   const std::string& message)
{
	return detail::internal_error<PUT>(file_type, org_id, uuid, message);
}

inline response<PUT>
server_busy(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return detail::server_busy<PUT>(file_type, org_id, uuid);
}

template<typename Time>
inline response<PUT>
server_busy(archiving::file_type::file_type_t file_type,
			archiving::org_id_t org_id,
			const std::string& uuid,
			const Time& retry_after)
{
	return detail::server_busy<PUT>(file_type, org_id, uuid, retry_after);
}

inline response<PUT>
insufficient_storage(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return detail::insufficient_storage<PUT>(file_type, org_id, uuid);
}

} // namespace put

namespace recv {

using common::net::archived::verb::RECV;
using detail::response;

inline response<RECV>
accepted(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return detail::accepted<RECV>(file_type, org_id, uuid);
}

inline response<RECV>
bad_request(archiving::file_type::file_type_t file_type,
			archiving::org_id_t org_id,
			const std::string& uuid,
			const std::string& message)
{
	return detail::bad_request<RECV>(file_type, org_id, uuid, message);
}

inline response<RECV>
bad_request(const std::string& message)
{
	return detail::bad_request<RECV>(message);
}

inline response<RECV>
internal_error(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return detail::internal_error<RECV>(file_type, org_id, uuid);
}

inline response<RECV>
internal_error(const std::string& message)
{
	return detail::internal_error<RECV>(message);
}

inline response<RECV>
internal_error(archiving::file_type::file_type_t file_type,
			   archiving::org_id_t org_id,
			   const std::string& uuid,
			   const std::string& message)
{
	return detail::internal_error<RECV>(file_type, org_id, uuid, message);
}

} // namespace put

} // namespace net
} // namespace archived
} // namespace archiving
#endif // ARCHIVED_PUT_RESPONSE_HPP
