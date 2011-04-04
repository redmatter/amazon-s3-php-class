/*
 * storage_operation.hpp
 *
 *  Created on: 6 Feb 2011
 *      Author: dkorah
 */

#ifndef STORAGE_OPERATION_HPP_
#define STORAGE_OPERATION_HPP_

#include "operation_data.hpp"

#include <rml/pc_channel.h>

#include <boost/function.hpp>

namespace app {

namespace storage {

struct operations
{

	// Order of the enum below decides operation queue priority as well; see operation_ex_compare
	//    hence the customer facing get_object set to be the first one; it does expose
	//    a potential DoS, but we should rate-limit such operations before it hits the
	//    ArchiveD
	enum operations_t
	{
		invalid = 0,
		get_object,
		put_object_init,
		put_object_commit,
		delete_object,
		stat_object
	};
};

struct operation_result
{
	enum operation_result_t { success, failure, interrupted };
};

template < typename object_identifier >
struct operation_ex
{
	typedef boost::function< void (boost::shared_ptr<object_identifier>, operation_result::operation_result_t) > callback_t;
	typedef object_identifier object_identifier_t;

	operations::operations_t operation;
	boost::shared_ptr<object_identifier> obj;
	callback_t callback;
};


namespace s3 {

typedef storage::operation_ex< operation_data > operation_ex;

struct operation_ex_compare
{
	bool operator() (const operation_ex& lhs, const operation_ex& rhs) {
		if(lhs.operation != rhs.operation)
			return lhs.operation < rhs.operation;

		return lhs.obj < rhs.obj;
	}
};

typedef rml::pc_channel<operation_ex, operation_ex_compare> operation_channel_t;

}  // namespace s3

}  // namespace storage

}  // namespace app

#endif /* STORAGE_OPERATION_HPP_ */
