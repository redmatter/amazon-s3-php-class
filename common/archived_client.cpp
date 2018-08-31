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
boost::random::mt19937 archived_client::rand_gen_;
boost::random::uniform_int_distribution<> archived_client::rand_dist_(1, 2000);

}  // namespace app



