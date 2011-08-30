/*
 * storage.hpp
 *
 *  Created on: 6 Feb 2011
 *      Author: dkorah
 */

#ifndef BASIC_STORAGE_HPP_
#define BASIC_STORAGE_HPP_

#include "storage_operation.hpp"
#include "storage_backend.hpp"

#include <boost/program_options.hpp>
#include <boost/noncopyable.hpp>

namespace app {

namespace storage {

template< typename Backend >
class basic_storage : public boost::noncopyable
{
	const boost::program_options::variables_map& confMap_;

public:
	typedef boost::function<void (const bool, operation_data::pointer&, boost::shared_ptr<std::fstream>)> fn_get_handler_t;

	basic_storage(const boost::program_options::variables_map& confMap) :
		confMap_(confMap)
	{
		// to avoid a race later while we go threaded
		(void)Backend::get_instance(confMap_);
	}

	bool commit_put(const operation_data::pointer& obj)
	{
		try {
			Backend::get_instance()->put(obj);
			return true;
		}
		catch(const std::exception& e) {
			LogError(obj<<": Failed commit to storage; err:"<<e.what());
		}
		return false;
	}

//	boost::shared_ptr<std::fstream> open_for_get(
//			object_identifier_t& obj,
//			const std::ios_base::openmode& mode = std::ios_base::in | std::ios_base::out | std::ios_base::binary | std::ios_base::trunc
//			)
//	{
//		Backend::get_instance()->get(obj);
//		return false;
////		return Backend::get_instance()->get(obj, operation::get_object, mode);
//	}
};

namespace s3 {

typedef basic_storage<s3::backend> storage;

}  // namespace s3

}  // namespace storage

}  // namespace app

#endif /* BASIC_STORAGE_HPP_ */
