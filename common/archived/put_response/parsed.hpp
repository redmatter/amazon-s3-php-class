//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 18/02/2020.
//

#ifndef ARCHIVING_COMMON_ARCHIVED_PUT_RESPONSE_PARSED_HPP
#define ARCHIVING_COMMON_ARCHIVED_PUT_RESPONSE_PARSED_HPP

#include "code.hpp"

#include "../../types.hpp"

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
	cannot_decode() : std::runtime_error("cannot_decode") {}
};

}

namespace common {
namespace archived {
namespace put_response {

struct parsed_response
{
	bool ok;

	std::string verb;

	archiving::file_type::file_type_t file_type;
	archiving::org_id_t org_id;
	std::string uuid;

	code_t code;
	std::string message;
	std::time_t retry_after;
};

inline std::istream&
operator>>(std::istream& is, parsed_response& r) try
{
	verify_char<char> vws(' ');
	verify_char<char> vlb('[');
	verify_char<char> vrb(']');
	verify_char<char> vnl('\n');

	std::string outcome;
	if (!(is >> outcome >> vws >> r.verb >> vws)) {
		throw cannot_decode();
	}

	r.ok = outcome == "OK";
	if (r.ok) {
		is >> r.file_type >> vws >> r.org_id >> vws >> r.uuid;

		r.code = unknown;
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
			is >> vnl;

			r.message = next_word + remaining_message;
			r.code = internal_error;
			r.retry_after = 0;

			return is;
		}
	} else {
		// extract the remainder of identifier-parts; we have already extracted file-type
		if (!(is >> vws >> r.org_id >> vws >> r.uuid)) {
			throw cannot_decode();
		}
	}

	// whitespace after identifier-parts
	if (!(is >> vws )) {
		throw cannot_decode();
	}

	char next_char = is.peek();
	if (next_char == EOF) {
		throw cannot_decode();
	}

	// if there is "[ <code> ... ]", then we are dealing with the new version of the protocol, otherwise we need to
	// continue working as we do now; i.e reconnect on any ERR
	if (next_char == '[') {
		if (!(is >> vlb >> vws >> r.code >> vws)) {
			throw cannot_decode();
		}

		next_char = is.peek();
		if (next_char == EOF) {
			throw cannot_decode();
		}

		if (next_char != ']') {
			std::string label;
			if (!std::getline(is, label, ':')) {
				return is;
			}

			if (label != "retry-after") {
				throw cannot_decode();
			}

			if (!(is >> r.retry_after >> vws)) {
				throw cannot_decode();
			}
		}

		if (!(is >> vrb >> vws)) {
			throw cannot_decode();
		}
	}

	std::getline(is, r.message, '\r');
	is >> vnl;

	return is;
} catch (...) {
	is.setstate(std::ios::failbit);
	return is;
}


} // namespace put_response
} // namespace archived
} // namespace common
#endif // ARCHIVING_COMMON_ARCHIVED_PUT_RESPONSE_PARSED_HPP
