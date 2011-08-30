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

struct operation
{

	// Order of the enum below decides operation queue priority as well; see operation_ex_compare
	//    hence the customer facing get_object set to be the first one; it does expose
	//    a potential DoS, but we should rate-limit such operations before it hits the
	//    ArchiveD
	enum operation_t
	{
		invalid = 0,
		get_object,
		put_object,
		delete_object,
		stat_object,

		n_operation
	};

	static inline const char* to_string(const operation_t& op)
	{
		static const char* str[] = {
				"invalid",
				"get_object",
				"put_object",
				"delete_object",
				"stat_object"
		};

		if(op > invalid && op < n_operation)
			return str[op];

		return str[0];
	}
};

struct operation_result
{
	enum operation_result_t { success, failure, interrupted };
};

struct operation_ex
{
	typedef boost::function< void (operation_data::pointer, operation_result::operation_result_t) > callback_t;

	operation::operation_t op;
	operation_data::pointer p_obj;
	callback_t callback;

	operation_ex(const operation::operation_t& op, const operation_data::pointer& p_obj, const callback_t& callback) :
		op(op), p_obj(p_obj), callback(callback)
	{
	}

	friend std::ostream& operator << (std::ostream& ostr, const operation_ex& opx)
	{
		return ostr << operation::to_string(opx.op) << ':' << opx.p_obj;
	}

};

struct operation_ex_compare
{
	bool operator() (const operation_ex& lhs, const operation_ex& rhs) {
		if(lhs.op != rhs.op)
			return lhs.op < rhs.op;

		operation_data::pointer_compare_lt obj_lt;

		return obj_lt(lhs.p_obj, rhs.p_obj);
	}
};

typedef rml::pc_channel<operation_ex, operation_ex_compare> operation_channel_t;

}  // namespace storage

}  // namespace app

#endif /* STORAGE_OPERATION_HPP_ */
