/*
 * archived.cpp
 *
 *  Created on: 29 Jan 2011
 *      Author: dkorah
 */

#include "archived.h"

#ifndef ARCHIVED_LISTEN_PORT
#define ARCHIVED_LISTEN_PORT "10100"
#endif

namespace archived {

const char* protocol_version_str = "1.0";
const int protocol_version[2] = {1, 0};

}  // namespace archived
