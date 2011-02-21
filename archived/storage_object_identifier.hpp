/*
 * storage_object_identifier.hpp
 *
 *  Created on: 8 Feb 2011
 *      Author: dkorah
 */

#ifndef STORAGE_OBJECT_IDENTIFIER_HPP_
#define STORAGE_OBJECT_IDENTIFIER_HPP_

#include <iostream>

#include <boost/shared_ptr.hpp>

#include <rml/utils.h>

#include "storage_backend.hpp"

namespace app {

namespace storage {

struct object_type {
	enum object_type_t {
		unknown = 0,
		clear,
		compressed_bz2,
		encrypted_blowfish,

		n_types
	};

	static const object_type_t& from_sring(const std::string& str) {
		if(str == "clear")
			return clear;
		else if(str == "compressed_bz2")
			return compressed_bz2;
		else if(str == "encrypted_blowfish")
			return encrypted_blowfish;
		else
			return unknown;
	}

	static const char* to_string(const object_type_t& type) {
		static const char* str = {
				"unknown",
				"clear",
				"compressed_bz2",
				"encrypted_blowfish"
		};

		if(type > unknown && type < n_types)
			return str[type];

		return str[0];
	}

	static const char* to_short_string(const object_type_t& type) {
		static const char* str = {
				"unk",
				"clr",
				"bz2",
				"bf"
		};

		if(type > unknown && type < n_types)
			return str[type];

		return str[0];
	}
};

template <typename object_identifier, typename key_type>
struct basic_object_identifier {

	typedef typename key_type key_t;

	key_t key;
	std::size_t content_length;
	object_type::object_type_t type;
	boost::shared_ptr<std::fstream> fstream;

	/**
	 * Gives a filesystem's filename compatible representation of identifier
	 * Handy way to derive temp filenames
	 *
	 * @param std::ostream& ostr
	 * @param const object_identifier& obj
	 *
	 * @return std::ostream&
	 */
	friend virtual std::ostream& operator<<(std::ostream& ostr, const object_identifier& obj) = 0;

	friend bool operator==(const basic_object_identifier& lhs, const basic_object_identifier& rhs) {
		return lhs.key == rhs.key;
	}
};

// specialisations

namespace s3 {

struct object_identifier : public basic_object_identifier< object_identifier, std::pair<std::string, std::string> >
{
	endpoint_description::pointer endpoint;

	friend std::ostream& operator<<(std::ostream& ostr, const object_identifier& obj) {
		return ostr << "s3:"<<obj.key.first<<':'<<obj.key.second;
	}
};

}  // namespace s3

}  // namespace storage

}  // namespace app

#endif /* STORAGE_OBJECT_IDENTIFIER_HPP_ */
