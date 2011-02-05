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

#include <rml/exception.h>

#include "md5.h"

namespace archiving {

typedef std::vector<unsigned char> hash_vector;

}

namespace std {

std::ostream& operator<<(std::ostream& ostr, const archiving::hash_vector& hash) {
	static const char* hex = "0123456789abcdef";
	archiving::hash_vector::const_iterator hash_end = hash.end();
	for(archiving::hash_vector::const_iterator itr = hash.begin(); itr != hash_end; ++itr) {
		ostr << hex[*itr >> 4];
		ostr << hex[*itr & 0x0f];
	}

	return ostr;
}

std::istream& operator>>(std::istream& istr, archiving::hash_vector& hv) {
	bool ok = true;

	archiving::hash_vector hv_(16);

	for(int i = 0; ok && (i < 16); ++i) {
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

	if(ok)
		std::copy(hv_.begin(), hv_.end(), hv.begin());

    return istr;
}

}

namespace archiving {

class md5hasher : public boost::noncopyable {

public:

	enum {
		max_buf_size = 40960,
		hash_bytes_size = 16
	};

	typedef boost::shared_ptr< md5hasher > pointer;

	inline md5hasher() : handle(new MD5()), hash_(hash_bytes_size), closed(false) {
		MD5Open(handle);
	}

	inline void digest(const char* const data, const unsigned int len) {
		if(closed)
			throw rml::exception("md5hash: already closed");

		MD5Digest(handle, data, len);
	}

	inline void close() {
		if(handle) {
			MD5Close(handle, &hash_[0]);
			delete handle; handle = 0;
		}

		closed = true;
	}

	inline const hash_vector& get_hash() const {
		if(!closed)
			LogWarning("hash not complete");

		return hash_;
	}

	inline ~md5hasher() {
		if(handle) {
			delete handle; handle = 0;
		}
	}

	static inline hash_vector hash(boost::shared_ptr<std::fstream>& fstr_ptr, const size_t& file_length_ = 0) {
		assert(fstr_ptr.get());
		return hash(*fstr_ptr, file_length_);
	}

	static hash_vector hash(std::fstream& fstr, const size_t& file_length_ = 0) {
		md5hasher hasher;
		size_t fLength, md5edLength;

		std::filebuf *pbuf = fstr.rdbuf();


		if(!file_length_) {
			fLength = 0;
			fLength = pbuf->pubseekoff(0,std::ios::end,std::ios::in);
			pbuf->pubseekpos(0,std::ios::in);
		}
		else
			fLength = file_length_;

		md5edLength = 0;

		char md5buf[max_buf_size];
		while(md5edLength < fLength) {
			std::streamsize blockSize = std::min<std::streamsize>((std::streamsize)max_buf_size, (std::streamsize)(fLength - md5edLength));
			blockSize = pbuf->sgetn(md5buf, blockSize);
			md5edLength += blockSize;
			hasher.digest(md5buf, blockSize);
		}

		if(md5edLength == fLength)
			hasher.close();
		else if(md5edLength > fLength)
			throw rml::exception("more than expected data MD5ed");

		return hash_vector(hasher.get_hash());
	}

private:
	MD5* handle;
	hash_vector hash_;

protected:
	bool closed;
};

} // namespace app

#endif /* MD5_HPP_ */
