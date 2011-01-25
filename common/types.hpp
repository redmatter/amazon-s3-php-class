/*
 * types.hpp
 *
 *  Created on: 25 Jan 2011
 *      Author: dkorah
 */

#ifndef TYPES_HPP_
#define TYPES_HPP_

#include <rml/pc_channel.h>

#include "archived_file.hpp"

namespace archiving {

typedef rml::pc_channel<archived_file::pointer> archived_file_channel_t;

}

#endif /* TYPES_HPP_ */
