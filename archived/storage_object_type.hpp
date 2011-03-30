/*
 * storage_object_type.hpp
 *
 *  Created on: 8 Feb 2011
 *      Author: dkorah
 */

#ifndef STORAGE_OBJECT_IDENTIFIER_HPP_
#define STORAGE_OBJECT_IDENTIFIER_HPP_

#include <iostream>

#include <boost/shared_ptr.hpp>

#include <rml/utils.h>

namespace app {

namespace storage {

struct object_type
{
	enum object_type_t
	{
		unknown = 0,
		clear,
		compressed_bz2,
		encrypted_blowfish,

		n_types
	};

	static object_type_t from_sring(const std::string& str)
	{
		if(str == "clear")
			return clear;
		else if(str == "compressed_bz2")
			return compressed_bz2;
		else if(str == "encrypted_blowfish")
			return encrypted_blowfish;
		else
			return unknown;
	}

	static const char* to_string(const object_type_t& type)
	{
		static const char* str[] = {
				"unknown",
				"clear",
				"compressed_bz2",
				"encrypted_blowfish"
		};

		if(type > unknown && type < n_types)
			return str[type];

		return str[0];
	}

	static const char* to_short_string(const object_type_t& type)
	{
		static const char* str[] = {
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

}  // namespace storage

}  // namespace app

#endif /* STORAGE_OBJECT_IDENTIFIER_HPP_ */
