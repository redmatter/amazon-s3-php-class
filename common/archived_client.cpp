/*
 * archived_client.cpp
 *
 *  Created on: 4 Oct 2011
 *      Author: dkorah
 */

#include "archived_client.hpp"

#include <boost/thread.hpp>

namespace app {

boost::mutex archived_client::mutex_;

bool archived_client::is_archived_ok_ = false;

}  // namespace app



