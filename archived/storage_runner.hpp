/*
 * storage_runner.hpp
 *
 *  Created on: 8 Feb 2011
 *      Author: dkorah
 */

#ifndef STORAGE_RUNNER_HPP_
#define STORAGE_RUNNER_HPP_

#include "storage_operation.hpp"
#include "storage.hpp"

#include <rml/exception.h>

namespace app {

namespace storage {

template < typename storage_t, typename channel_t >
class basic_runner
{
	typedef typename channel_t::item_t operation_ex_t;

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
				storage::operation_result::operation_result_t result = storage::operation_result::failure;
				try {
					switch(op_obj.op) {
					case operation::put_object:
						result = storage_.commit_put(op_obj.p_obj)?
								storage::operation_result::success:
								storage::operation_result::failure;
						break;

//					case operation::get_object:
//						(void)storage_.open_for_get(op_obj.p_obj);
//						break;

					default:
						THROW_RML_EXCEPTION("Invalid operation ["<<op_obj.op<<']');
						/* no break */
					}
				}
				catch(const std::exception& e) {
					LogError("exception:"<<e.what());
				}
				catch(...) {
					LogCritical("unexpected exception");
				}

				// do call back to handle result
				op_obj.callback(op_obj.p_obj, result);
			}
			catch(const typename channel_t::finishing_exception& e) {
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

class runner : public basic_runner<s3::storage, operation_channel_t>
{
public:
	enum { min_threads = 2 };

	runner(s3::storage& storage, operation_channel_t& channel, volatile bool& quit) :
		basic_runner<s3::storage, operation_channel_t>(storage, channel, quit)
	{
		LogVerbose("S3 Runner created");
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
