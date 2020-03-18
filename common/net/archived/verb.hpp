//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 19/02/2020.
//

#ifndef COMMON_NET_ARCHIVED_VERB_HPP
#define COMMON_NET_ARCHIVED_VERB_HPP

#include <iostream>

namespace common {
namespace net {
namespace archived {
namespace verb {

struct PUT;
struct RECV;

namespace detail {

namespace response = common::net::archived::response;

template <typename Verb>
struct ok;

template <>
struct ok<PUT> {
	static const response::code_t code = response::continue_;
};

template <>
struct ok<RECV> {
	static const response::code_t code = response::accepted;
};

} // namespace detail

struct PUT {
	typedef detail::ok<PUT> ok;
};

std::ostream&
operator<<(std::ostream& os, const PUT&)
{
	return os << "PUT";
}

std::istream&
operator>>(std::istream& is, PUT&)
{
	std::string verb;
	if (is >> verb && verb != "PUT") {
		is.setstate(std::ios::failbit);
	}

	return is;
}

struct RECV {
	typedef detail::ok<RECV> ok;
};

std::ostream&
operator<<(std::ostream& os, const RECV&)
{
	return os << "RECV";
}

std::istream&
operator>>(std::istream& is, RECV&)
{
	std::string verb;
	if (is >> verb && verb != "RECV") {
		is.setstate(std::ios::failbit);
	}

	return is;
}

} // namespace verb
} // namespace archived
} // namespace net
} // namespace common

#endif // COMMON_NET_ARCHIVED_VERB_HPP
