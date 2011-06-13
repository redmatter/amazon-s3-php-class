/*
 * defines.h
 *
 *  Created on: 29 Jan 2011
 *      Author: dkorah
 */

#ifndef DEFINES_H_
#define DEFINES_H_

namespace archiving {

extern const char* put_protocol_version_str;
extern const int put_protocol_version[2];

struct ports {
	enum ports_t {
		put = 10100,
		get = 10101,

		monitoring = 11111
	};
};

}  // namespace archiving


#ifndef NDEBUG

#define SetupRecvStream(from, istr, recv_buffer) \
	std::string line; \
	{ \
		std::istream istr_dup(&recv_buffer); \
		std::getline(istr_dup, line, '\n'); \
		LogDebug(from" -> "<<line); \
	} \
	\
	std::istringstream istr(line+'\n'); \
	istr.unsetf(std::ios_base::skipws)

#else

#define SetupRecvStream(from, istr, recv_buffer) \
	std::istream istr(&recv_buffer); \
	istr.unsetf(std::ios_base::skipws)

#endif


#endif /* DEFINES_H_ */
