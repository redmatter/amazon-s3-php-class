//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 17/02/2020.
//

#ifndef ARCHIVING_COMMON_ARCHIVED_PUT_RESPONSE_HPP
#define ARCHIVING_COMMON_ARCHIVED_PUT_RESPONSE_HPP
namespace common {
namespace archived {
namespace put_response {

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

} // namespace put_response
} // namespace archived
} // namespace common
#endif // ARCHIVING_COMMON_ARCHIVED_PUT_RESPONSE_HPP
