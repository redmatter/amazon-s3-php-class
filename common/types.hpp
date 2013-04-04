/*
 * types.hpp
 *
 *  Created on: 25 Jan 2011
 *      Author: dkorah
 */

#ifndef ARCHIVING_TYPES_HPP_
#define ARCHIVING_TYPES_HPP_

#include <cryptopp/cryptlib.h>
#include <cryptopp/filters.h>
#include <cryptopp/modes.h>
#include <cryptopp/files.h>
#include <cryptopp/md5.h>
#include <cryptopp/base64.h>
#include <cryptopp/hex.h>

#include <boost/array.hpp>

#include <stdint.h>

namespace archiving {

typedef uint32_t org_id_t;
typedef int32_t user_id_t;
typedef int32_t device_id_t;
typedef uint32_t group_id_t;

typedef uint64_t endpoint_id_t;

typedef boost::array<byte, CryptoPP::Weak1::MD5::DIGESTSIZE> hash_vector;

typedef boost::array< byte, 16 > uuid_t;

/**
 * Types of files we deal with
 *
 * IMPORTANT: Dont forget to update policy::pseudo_id enum
 */
struct file_type
{
	enum file_type_t
	{
		unknown = 0,
		cdr,
		recording,
		sms,
		buffered_recording,
		pcap,
		info,

		n_file_types
	};

	/**
	 * to string for of the type
	 *
	 * @param type
	 * @return
	 */
	static const char* to_string(const file_type_t type);

	/**
	 * from string to type
	 *
	 * @param str
	 * @return
	 */
	static const file_type_t from_string(const std::string& str);
};

}

namespace std {

ostream& operator<<(ostream& ostr, const archiving::hash_vector& hash);

istream& operator>>(istream& istr, archiving::hash_vector& hv);

istream& operator>>(istream& istr, archiving::file_type::file_type_t& t);

ostream& operator<<(ostream& ostr, const archiving::file_type::file_type_t& type);

}

#endif /* ARCHIVING_TYPES_HPP_ */
