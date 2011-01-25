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

}

namespace archiving {

class md5hasher : public boost::noncopyable {

public:

	enum {
		max_buf_size = 40960,
		hash_bytes_size = 16
	};

	typedef boost::shared_ptr< md5hasher > pointer;

	md5hasher() : handle(new MD5()), hash_(hash_bytes_size), closed(false) {
		MD5Open(handle);
	}

	void digest(const char* const data, const unsigned int len) {
		if(closed)
			throw rml::exception("md5hash: already closed");

		MD5Digest(handle, data, len);
	}

	void close() {
		if(handle) {
			MD5Close(handle, &hash_[0]);
			delete handle; handle = 0;
		}

		closed = true;
	}

	hash_vector get_hash() const {
		if(!closed)
			LogWarning("hash not complete");

		return hash_vector(hash_);
	}

	~md5hasher() {
		if(handle) {
			delete handle; handle = 0;
		}
	}

	static std::vector<unsigned char> hash(std::ifstream& ifs) {
		md5hasher hasher;
		size_t fLength, md5edLength;

		std::filebuf *pbuf = ifs.rdbuf();

		fLength = 0;
		fLength = pbuf->pubseekoff(0,std::ios::end,std::ios::in);
		pbuf->pubseekpos(0,std::ios::in);

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

		return std::vector<unsigned char>(hasher.get_hash());
	}

private:
	MD5* handle;
	hash_vector hash_;

protected:
	bool closed;
};

} // namespace app

#endif /* MD5_HPP_ */
