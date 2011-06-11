/*
 * md5.hpp
 *
 *  Created on: 24 Jan 2011
 *      Author: dkorah
 */

#ifndef MD5_HPP_
#define MD5_HPP_

#include <algorithm>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>

#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/array.hpp>

#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#include <cryptopp/cryptlib.h>
#include <cryptopp/filters.h>
#include <cryptopp/modes.h>
#include <cryptopp/files.h>
#include <cryptopp/md5.h>
#include <cryptopp/base64.h>
#include <cryptopp/hex.h>
#undef CRYPTOPP_ENABLE_NAMESPACE_WEAK

#include <rml/exception.h>

namespace archiving {

typedef boost::array<byte, CryptoPP::Weak1::MD5::DIGESTSIZE> hash_vector;

}

namespace std {

std::ostream& operator<<(std::ostream& ostr, const archiving::hash_vector& hash)
{
	string ret;

	CryptoPP::HexEncoder encoder;
	encoder.Put(&hash[0], hash.size());

	size_t size = encoder.MaxRetrievable();
	if(size) {
	    ret.resize(size);
	    encoder.Get((byte*)&ret[0], ret.size());
	}

	return ostr << ret;
}

std::istream& operator>>(std::istream& istr, archiving::hash_vector& hv)
{
	bool ok = true;

	archiving::hash_vector hv_;

	for(int i = 0; ok && (i < CryptoPP::Weak1::MD5::DIGESTSIZE); ++i) {
		hv_[i] = '\0';
		for(int j = 0; ok && (j < 2); ++j) {
			char c; istr >> c;

			unsigned char uc;

			if(c >= '0' && c <= '9')
				uc = (unsigned char)(c - '0');
			else if(c >= 'a' && c <= 'f')
				uc = (unsigned char)(9 + c - 'a');
			else if(c >= 'A' && c <= 'F')
				uc = (unsigned char)(9 + c - 'A');
			else
				ok = false;

			if(ok)
				hv_[i] |= uc << (j == 0? 4 : 0);
		}
	}

	if(!ok)
		throw rml::exception("invalid hash");
	
	hv = hv_;

    return istr;
}

}

namespace archiving {

class md5hasher : public boost::noncopyable
{

public:

	typedef boost::shared_ptr< md5hasher > pointer;

	static inline hash_vector hash(rml::utils::tmp_fstream::pointer fstr_ptr, const size_t& file_length_ = 0)
	{
		assert(fstr_ptr.get());
		return hash(*fstr_ptr, file_length_? file_length_: fstr_ptr->get_size());
	}

	static hash_vector hash(std::fstream& fstr, const size_t& file_length_ = 0)
	{
		hash_vector ret;

		CryptoPP::Weak1::MD5 hash;
		CryptoPP::FileSource f(fstr, true,
				new CryptoPP::HashFilter(hash,
						new CryptoPP::ArraySink(&ret[0], ret.size())));

		return ret;
	}
};

} // namespace app

#endif /* MD5_HPP_ */
