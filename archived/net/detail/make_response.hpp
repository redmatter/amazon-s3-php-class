//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 19/02/2020.
//

#ifndef ARCHIVING_ARCHIVED_NET_DETAIL_MAKE_RESPONSE_HPP
#define ARCHIVING_ARCHIVED_NET_DETAIL_MAKE_RESPONSE_HPP

#include "response.hpp"
#include "../../../common/utils/time.hpp"

#include <boost/chrono/chrono.hpp>

namespace archiving {
namespace archived {
namespace net {
namespace detail {

template <typename Verb>
inline response<Verb>
continue_(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return response<Verb>(file_type, org_id, uuid, response_code::continue_);
}

template <typename Verb>
inline response<Verb>
accepted(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return response<Verb>(file_type, org_id, uuid, response_code::accepted);
}

template <typename Verb>
inline response<Verb>
bad_request(archiving::file_type::file_type_t file_type,
			archiving::org_id_t org_id,
			const std::string& uuid,
			const std::string& message)
{
	return response<Verb>(file_type, org_id, uuid, response_code::bad_request, message);
}

template <typename Verb>
inline response<Verb>
bad_request(const std::string& message)
{
	return response<Verb>(response_code::bad_request, message);
}

template <typename Verb>
inline response<Verb>
internal_error(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return response<Verb>(file_type, org_id, uuid, response_code::internal_error, "internal server error");
}

template <typename Verb>
inline response<Verb>
internal_error(const std::string& message)
{
	return response<Verb>(response_code::internal_error, message);
}

template <typename Verb>
inline response<Verb>
internal_error(archiving::file_type::file_type_t file_type,
			   archiving::org_id_t org_id,
			   const std::string& uuid,
			   const std::string& message)
{
	return response<Verb>(file_type, org_id, uuid, response_code::internal_error, message);
}

template <typename Verb>
inline response<Verb>
server_busy(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return response<Verb>(file_type, org_id, uuid, response_code::server_busy, "server busy");
}

template<typename Verb, typename Clock>
inline response<Verb>
server_busy(archiving::file_type::file_type_t file_type,
			archiving::org_id_t org_id,
			const std::string& uuid,
			const boost::chrono::time_point<Clock>& retry_after)
{
	using common::utils::to_time_t;
	return response<Verb>(file_type, org_id, uuid, response_code::server_busy, "server busy", to_time_t(retry_after));
}

template <typename Verb>
inline response<Verb>
server_busy(archiving::file_type::file_type_t file_type,
			archiving::org_id_t org_id,
			const std::string& uuid,
			const std::time_t& retry_after)
{
	return response<Verb>(file_type, org_id, uuid, response_code::server_busy, "server busy", retry_after);
}

template <typename Verb>
inline response<Verb>
insufficient_storage(archiving::file_type::file_type_t file_type, archiving::org_id_t org_id, const std::string& uuid)
{
	return response<Verb>(file_type, org_id, uuid, response_code::insufficient_storage, "not enough space on disk");
}

} // namespace detail
} // namespace net
} // namespace archived
} // namespace archiving
#endif // ARCHIVING_ARCHIVED_NET_DETAIL_MAKE_RESPONSE_HPP
