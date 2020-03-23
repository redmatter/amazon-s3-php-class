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

typedef common::net::archived::verb::PUT verb;
typedef detail::response<verb> response;

inline response
continue_(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return detail::continue_<verb>(file_type, org_id, uuid);
}

inline response
bad_request(archiving::file_type::file_type_t file_type,
			archiving::org_id_t org_id,
			const std::string& uuid,
			const std::string& message)
{
	return detail::bad_request<verb>(file_type, org_id, uuid, message);
}

inline response
bad_request(const std::string& message)
{
	return detail::bad_request<verb>(message);
}

inline response
internal_error(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return detail::internal_error<verb>(file_type, org_id, uuid);
}

inline response
internal_error(const std::string& message)
{
	return detail::internal_error<verb>(message);
}

inline response
internal_error(archiving::file_type::file_type_t file_type,
			   archiving::org_id_t org_id,
			   const std::string& uuid,
			   const std::string& message)
{
	return detail::internal_error<verb>(file_type, org_id, uuid, message);
}

inline response
server_busy(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return detail::server_busy<verb>(file_type, org_id, uuid);
}

template<typename Time>
inline response
server_busy(archiving::file_type::file_type_t file_type,
			archiving::org_id_t org_id,
			const std::string& uuid,
			const Time& retry_after)
{
	return detail::server_busy<verb>(file_type, org_id, uuid, retry_after);
}

inline response
insufficient_storage(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return detail::insufficient_storage<verb>(file_type, org_id, uuid);
}

} // namespace put

namespace recv {

typedef common::net::archived::verb::RECV verb;
typedef detail::response<verb> response;

inline response
accepted(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return detail::accepted<verb>(file_type, org_id, uuid);
}

inline response
bad_request(archiving::file_type::file_type_t file_type,
			archiving::org_id_t org_id,
			const std::string& uuid,
			const std::string& message)
{
	return detail::bad_request<verb>(file_type, org_id, uuid, message);
}

inline response
bad_request(const std::string& message)
{
	return detail::bad_request<verb>(message);
}

inline response
internal_error(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return detail::internal_error<verb>(file_type, org_id, uuid);
}

inline response
internal_error(const std::string& message)
{
	return detail::internal_error<verb>(message);
}

inline response
internal_error(archiving::file_type::file_type_t file_type,
			   archiving::org_id_t org_id,
			   const std::string& uuid,
			   const std::string& message)
{
	return detail::internal_error<verb>(file_type, org_id, uuid, message);
}

} // namespace recv

} // namespace net
} // namespace archived
} // namespace archiving
#endif // ARCHIVED_PUT_RESPONSE_HPP
