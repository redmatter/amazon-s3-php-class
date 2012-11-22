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

}

namespace std {

ostream& operator<<(ostream& ostr, const archiving::hash_vector& hash);

istream& operator>>(istream& istr, archiving::hash_vector& hv);

}

#endif /* ARCHIVING_TYPES_HPP_ */
