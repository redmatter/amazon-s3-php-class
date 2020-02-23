//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 18/02/2020.
//

#ifndef COMMON_NET_ARCHIVED_RESPONSE_PARSED_HPP
#define COMMON_NET_ARCHIVED_RESPONSE_PARSED_HPP

#include "code.hpp"
#include "../verb.hpp"

#include "../../../types.hpp"

#include <boost/io/ios_state.hpp>

#include <ctime>
#include <iostream>
#include <string>

namespace {
template<typename Char>
struct verify_char
{
	const Char c;
	explicit verify_char(Char c) : c(c) {}
};

template<typename Char>
inline std::istream&
operator>>(std::basic_istream<Char>& is, verify_char<Char>& wss)
{
	if (wss.c != is.get()) {
		is.setstate(std::ios::failbit);
	}

	return is;
}

struct cannot_decode : public std::runtime_error
{
	int line;
	explicit cannot_decode(int line) : std::runtime_error("cannot_decode"), line(line) {}
};

}

namespace common {
namespace net {
namespace archived {
namespace detail {

namespace response_code = common::net::archived::response;
using response_code::code_t;

template <typename Verb>
struct response
{
	typedef Verb verb_type;

	bool ok;

	archiving::file_type::file_type_t file_type;
	archiving::org_id_t org_id;
	std::string uuid;

	code_t code;
	std::string message;
	std::time_t retry_after;
};

template <typename Verb>
inline std::ostream&
operator<<(std::ostream& os, const response<Verb>& r)
{
	typename response<Verb>::verb_type verb;

	os << (r.ok ? "OK " : "ERR ") << verb;
	if (!r.uuid.empty()) {
		os << ' ' << r.file_type << ' ' << r.org_id << ' ' << r.uuid;
	}

	os << " [ code:" << r.code;
	if (r.retry_after) {
		os << " retry-after:" << r.retry_after;
	}
	if (!r.message.empty()) {
		os << " message:" << r.message;
	}

	return os << " ]";
}

template <typename Verb>
inline std::istream&
operator>>(std::istream& is, response<Verb>& r)
try {
	typedef typename Verb::ok ok;

	Verb verb;

	verify_char<char> space(' ');
	verify_char<char> left_bracket('[');
	verify_char<char> right_bracket(']');
	verify_char<char> newline('\n');

	std::string outcome;
	if (!(is >> outcome >> space >> verb >> space)) {
		throw cannot_decode(__LINE__);
	}

	r.ok = outcome == "OK";
	if (r.ok) {
		r.code = ok::code;

		is >> r.file_type >> space >> r.org_id >> space >> r.uuid;

		r.message.clear();
		r.retry_after = 0;

		return is;
	}

	std::string next_word;
	is >> next_word;

	bool next_word_unknown = next_word == "<unknown>";
	bool no_identifier_parts = next_word_unknown;
	if (!no_identifier_parts) {
		// we could still be talking to an old ArchiveD
		// If conversion of the next word to file-type fails, then there are no more stuff to read; just the message
		try {
			r.file_type = archiving::file_type::from_string(next_word);
		} catch (...) {
			no_identifier_parts = true;
		}
	}

	if (no_identifier_parts) {
		r.file_type = archiving::file_type::unknown;
		r.org_id = 0;
		r.uuid.clear();

		// we are definitely dealing with an old ArchiveD here.
		if (!next_word_unknown) {
			std::string remaining_message;
			std::getline(is, remaining_message, '\r');
			is >> newline;

			r.message = next_word + remaining_message;
			r.code = response_code::internal_error;
			r.retry_after = 0;

			return is;
		}
	} else {
		// extract the remainder of identifier-parts; we have already extracted file-type
		if (!(is >> space >> r.org_id >> space >> r.uuid)) {
			throw cannot_decode(__LINE__);
		}
	}

	// whitespace after identifier-parts
	if (!(is >> space)) {
		throw cannot_decode(__LINE__);
	}

	char next_char = is.peek();
	if (next_char == EOF) {
		throw cannot_decode(__LINE__);
	}

	// if there is "[ <code> ... ]", then we are dealing with the new version of the protocol, otherwise we need to
	// continue working as we do now; i.e reconnect on any ERR
	if (next_char == '[') {
		if (!(is >> left_bracket >> space >> r.code >> space)) {
			throw cannot_decode(__LINE__);
		}

		next_char = is.peek();
		if (next_char == EOF) {
			throw cannot_decode(__LINE__);
		}

		if (next_char != ']') {
			std::string label;
			if (!std::getline(is, label, ':')) {
				return is;
			}

			if (label != "retry-after") {
				throw cannot_decode(__LINE__);
			}

			if (!(is >> r.retry_after >> space)) {
				throw cannot_decode(__LINE__);
			}
		}

		if (!(is >> right_bracket >> space)) {
			throw cannot_decode(__LINE__);
		}
	}

	std::getline(is, r.message, '\r');
	is >> newline;

	return is;
} catch (const cannot_decode& e) {
	DevDebug("response-parsing: cannot_decode on line " << e.line);
	is.setstate(std::ios::failbit);
	return is;
} catch (...) {
	DevDebug("response-parsing: unknown exception");
	is.setstate(std::ios::failbit);
	return is;
}
} // namespace detail

namespace put {
typedef common::net::archived::verb::PUT verb_type;
typedef detail::response<verb_type> response;
} // namespace put

namespace recv {
typedef common::net::archived::verb::RECV verb_type;
typedef detail::response<verb_type> response;
} // namespace recv

} // namespace archived
} // namespace net
} // namespace common
#endif // COMMON_NET_ARCHIVED_RESPONSE_PARSED_HPP
