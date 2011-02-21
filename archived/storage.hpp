/*
 * storage.hpp
 *
 *  Created on: 6 Feb 2011
 *      Author: dkorah
 */

#ifndef BASIC_STORAGE_HPP_
#define BASIC_STORAGE_HPP_

#include <boost/program_options.hpp>
#include <boost/noncopyable.hpp>

#include "storage_operation.hpp"
#include "storage_backend.hpp"

namespace app {

namespace storage {

template< typename Backend, typename object_identifier_t >
class basic_storage : public boost::noncopyable
{
	const boost::program_options::variables_map& confMap_;

public:
	typedef boost::function<void (const bool, object_identifier_t&, boost::shared_ptr<std::fstream>)> fn_get_handler_t;

	basic_storage(const boost::program_options::variables_map& confMap) :
		confMap_(confMap)
	{
		// to avoid a race later while we go threaded
		(void)Backend::get_instance(confMap_);
	}

	boost::shared_ptr<std::fstream> open_for_put(
			object_identifier_t& obj,
			const std::ios_base::openmode& mode = std::ios_base::in | std::ios_base::out | std::ios_base::binary | std::ios_base::trunc
			)
	{
		return Backend::get_instance()->open(obj, operation::put_object_init, mode);
	}

	bool commit_put(object_identifier_t& obj)
	{
		return Backend::get_instance()->commit_put(obj);
	}

	boost::shared_ptr<std::fstream> open_for_get(
			object_identifier_t& obj,
			const std::ios_base::openmode& mode = std::ios_base::in | std::ios_base::out | std::ios_base::binary | std::ios_base::trunc
			)
	{
		return Backend::get_instance()->open(obj, operation::get_object, mode);
	}
};

namespace s3 {

typedef basic_storage<s3::backend, s3::object_identifier> storage;

}  // namespace s3

}  // namespace storage

}  // namespace app

#endif /* BASIC_STORAGE_HPP_ */
