/*
 * archived_client.cpp
 *
 *  Created on: 4 Oct 2011
 *      Author: dkorah
 */

#include "archived_client.hpp"

#include <boost/atomic.hpp>

namespace app {

boost::atomic_uint archived_client::n_instances_(0U);
boost::atomic_uint archived_client::n_ok_(0U);
boost::atomic_uint archived_client::instance_id_seq_(0U);

}  // namespace app



