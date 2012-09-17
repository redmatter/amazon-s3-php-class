/*
 * retention_purge.hpp
 *
 *  Created on: 23/08/2012
 *      Author: dkorah
 */

#include "retention_purge.hpp"

namespace app {

namespace retention_purge {

item::weak_set_t item::save_db_set;
boost::mutex item::save_db_mutex;

}  // namespace retention_purge

}  // namespace app
