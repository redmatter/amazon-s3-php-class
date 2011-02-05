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

#endif /* DEFINES_H_ */
