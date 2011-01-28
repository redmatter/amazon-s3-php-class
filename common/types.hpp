/*
 * types.hpp
 *
 *  Created on: 25 Jan 2011
 *      Author: dkorah
 */

#ifndef TYPES_HPP_
#define TYPES_HPP_

#include <rml/pc_channel.h>

#include "archivable.hpp"

namespace archiving {

typedef rml::pc_channel<archivable::pointer, archivable_pointer_compare> archivable_channel_t;

}

#endif /* TYPES_HPP_ */
