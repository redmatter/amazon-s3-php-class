//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 17/02/2020.
//

#ifndef COMMON_NET_ARCHIVED_RESPONSE_HPP
#define COMMON_NET_ARCHIVED_RESPONSE_HPP
#include <iostream>
namespace common {
namespace net {
namespace archived {
namespace response {

// Response codes for PUT
// Semantics are kept as close as possible to that of HTTP status codes.
enum code_t
{
	unknown = 0,

	// TODO: OK responses
	// These don't need any fixing / improvements right now, so they are left alone for a later date, for the sake
	// of avoiding any breakages.
	continue_ = 100,
	accepted = 202,

	// ERR responses

	// ERR file-type org-id uuid
	bad_request = 400,

	internal_error = 500, // uncategorised internal error on server
	server_busy = 503,	  // similar to HTTP Service Unavailable with Retry-After
	insufficient_storage = 507
};

inline std::istream&
operator>>(std::istream& is, code_t& c) {
	unsigned short code_;
	if (!(is >> code_)) {
		return is;
	}

	switch (code_) {
		case 100:
			c = continue_;
			break;
		case 202:
			c = accepted;
			break;

		case 400:
			c = bad_request;
			break;

		case 500:
			c = internal_error;
			break;
		case 503:
			c = server_busy;
			break;
		case 507:
			c = insufficient_storage;
			break;

		default:
			is.setstate(std::ios::failbit);
			break;
	}

	return is;
}

} // namespace response
} // namespace archived
} // namespace net
} // namespace common
#endif // COMMON_NET_ARCHIVED_RESPONSE_HPP
