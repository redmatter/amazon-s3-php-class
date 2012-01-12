/*
 * archived_client.cpp
 *
 *  Created on: 4 Oct 2011
 *      Author: dkorah
 */

#include "archived_client.hpp"

#include <boost/thread.hpp>

namespace app {

unsigned int archived_client::n_instances_ = 0;
unsigned int archived_client::n_ok_ = 0;

}  // namespace app



