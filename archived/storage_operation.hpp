/*
 * storage_operation.hpp
 *
 *  Created on: 6 Feb 2011
 *      Author: dkorah
 */

#ifndef STORAGE_OPERATION_HPP_
#define STORAGE_OPERATION_HPP_

namespace app {

namespace storage {

struct operation {
	enum operation_t {
		invalid = 0,
		put_object_init,
		put_object_commit,
		get_object,
		delete_object,
		stat_object
	};
};

}  // namespace storage

}  // namespace app

#endif /* STORAGE_OPERATION_HPP_ */
