/*
 * md5.hpp
 *
 *  Created on: 24 Jan 2011
 *      Author: dkorah
 */

#ifndef MD5_HPP_
#define MD5_HPP_

#include <algorithm>
#include <fstream>

#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>

#include <rml/exception.h>

#include "md5.h"

namespace std {
std::ostream& operator<<(std::ostream& ostr, const md5hasher& md5);
}

namespace app {

class md5hasher : public boost::noncopyable {
	MD5* handle;

	enum {
		max_buf_size = 40960
	};

protected:
	bool closed;
	unsigned char hash[16];

	friend std::ostream& operator<<(std::ostream& ostr, const md5hasher& md5);

public:
	typedef boost::shared_ptr< md5hasher > pointer;

	md5hasher() : handle(new MD5()), closed(false) {
		MD5Open(handle);
	}

	void digest(const char const* data, const unsigned int len) {
		if(closed)
			throw rml::exception("md5hash: already closed");

		MD5Digest(handle, data, len);
	}

	void close() {
		if(handle) {
			MD5Close(handle, hash);
			delete handle; handle = 0;
		}

		closed = true;
	}

	unsigned const char const * get_hash() const {
		return hash;
	}

	~md5hasher() {
		if(handle) {
			delete handle; handle = 0;
		}
	}

	static pointer hash(std::ifstream& ifs) {
		pointer hasher(new md5hasher);
		size_t fLength, md5edLength;

		std::filebuf *pbuf = ifs.rdbuf();
		fLength = 0;
		fLength = pbuf.pubseekoff(0,std::ios::end,std::ios::in);
		pbuf.pubseekpos(0,std::ios::in);
		md5edLength = 0;

		char md5buf[max_buf_size];
		while(md5edLength < fLength) {
			std::streamsize blockSize = std::min<std::streamsize>((std::streamsize)max_buf_size,(std::streamsize)(fLength-md5edLength));
			blockSize = pbuf->sgetn(md5buf, blockSize);
			md5edLength += blockSize;
			hasher->digest(md5buf, blockSize);
		}

		if(md5edLength == fLength)
			hasher->close();
		else if(md5edLength > fLength)
			throw rml::exception("more than expected data MD5ed");

		return hasher;
	}
};

namespace std {

std::ostream& operator<<(std::ostream& ostr, const md5hasher& md5) {
	static const char const hex[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
	if(md5.closed) {
		for(int i = 0; i<16; ++i) {
			ostr << hex[md5.hash >> 4];
			ostr << hex[md5.hash & 0x0f];
		}
	}
	else
		ostr<<"---------not-closed-yet---------";

	return ostr;
}

}  // namespace std

} // namespace app

#endif /* MD5_HPP_ */
