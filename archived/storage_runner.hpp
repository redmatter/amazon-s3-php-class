/*
 * storage_runner.hpp
 *
 *  Created on: 8 Feb 2011
 *      Author: dkorah
 */

#ifndef STORAGE_RUNNER_HPP_
#define STORAGE_RUNNER_HPP_

#include <rml/exception.h>

#include "storage_operation.hpp"
#include "storage.hpp"

namespace app {

namespace storage {

template < typename storage_t, typename channel_t >
class basic_runner
{
	typedef channel_t::item_t operation_ex_t;
	typedef channel_t::item_t::object_identifier_t object_identifier_t;

	storage_t& storage_;
	channel_t& channel_;

	volatile const bool& quit_;

public:
	basic_runner(storage_t& storage, channel_t& channel, volatile const bool& quit) :
		storage_(storage), channel_(channel), quit_(quit)
	{
		//ctor
	}

	virtual ~basic_runner() {}

	void run()
	{
		while(!quit_) {
			try {
				operation_ex_t op_obj = channel_.consume();
				bool result = false;
				try {
					switch(op_obj.operation) {
					case operation::put_object_commit:
						result = storage_.commit_put(*(op_obj.obj.get()));
						break;

					case operation::get_object:
						(void)storage_.open_for_get(*(op_obj.obj.get()));
						break;

					default:
						THROW_RML_EXCEPTION("Invalid operation ["<<op_obj.operation<<']');
					}
				}
				catch(const std::exception& e) {
					LogError("exception:"<<e.what());
				}
				catch(...) {
					LogCritical("unexpected exception");
				}
			}
			catch(const channel_t::finishing_exception& e) {
				LogVerbose(e.what());
				break;
			}
			catch(const std::exception& e) {
				LogError("exception:"<<e.what());
			}
			catch(...) {
				LogCritical("unexpected exception");
			}
		}
	}
};

namespace s3 {

class runner : public basic_runner<storage, operation_channel_t>
{
public:
	enum { min_threads = 2 };

	runner(storage_t& storage, channel_t& channel, volatile bool& quit) :
		basic_runner<storage, operation_channel_t>(storage, channel, quit)
	{
		LogMessage("S3 Runner created");
	}

	static void describe_config(boost::program_options::options_description& optsDesc) {
		optsDesc.add_options()
				("s3_runner.threads",  boost::program_options::value<unsigned int>()->default_value(min_threads), "S3 runner threads")
				;
	}
};

}  // namespace s3

}  // namespace storage

}  // namespace app

#endif /* STORAGE_RUNNER_HPP_ */
