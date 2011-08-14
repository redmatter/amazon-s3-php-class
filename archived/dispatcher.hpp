/*
 * dispatcher.hpp
 *
 * dispatcher dispatches various requests according to their destination.
 * It binds the handler functor to operation_data representation of a request
 *
 *  Created on: 24 May 2011
 *      Author: dkorah
 */

#ifndef DISPATCHER_HPP_
#define DISPATCHER_HPP_

#include "storage_operation.hpp"
#include "operation_data.hpp"

#include <boost/bind.hpp>

namespace app {

class dispatcher
{
	operation_data::channel_t& convert_done_channel_;
	storage::s3::operation_channel_t& s3_operation_channel_;
	operation_data::channel_t& retention_data_channel_;
	// operation_data::channel_t& get_data_ready_channel_;

	database::internal& internal_db_;

	volatile bool& quit_;

public:
	dispatcher(
			operation_data::channel_t& convert_done_channel,
			storage::s3::operation_channel_t& s3_operation_channel,
			operation_data::channel_t& retention_data_channel,
			// operation_data::channel_t& get_data_ready_channel,
			database::internal& internal_db,
			volatile bool& quit) :
				convert_done_channel_(convert_done_channel),
				s3_operation_channel_(s3_operation_channel),
				retention_data_channel_(retention_data_channel),
				// get_data_ready_channel_(get_data_ready_channel),
				internal_db_(internal_db),
				quit_(quit)
	{
	}

	void queue_for_retention_save(operation_data::pointer opdata, const storage::operation_result::operation_result_t& storage_opr_result)
	{
		if(storage_opr_result == storage::operation_result::success) {
			LogVerbose(opdata->p_detail->identifier<<": Saved to S3; queueing for retention");
			opdata->state = operation_state::operation_state_t(opdata->state + 1);
			internal_db_.save(opdata); // save before we potentially hand off to another thread
			retention_data_channel_.produce(opdata);
		}
		else {
			bool error = false;
			LogVerbose(opdata->p_detail->identifier<<": Saving to S3 failed");
			opdata->p_detail->retry++;
			if(opdata->p_detail->retry > 3) { // max retry reached; mark error
				error = true;
				opdata->state = operation_state::operation_state_t(opdata->state + 2);
				LogError(opdata->p_detail->identifier<<": Failed store after "<<opdata->p_detail->retry<<" retry(s)");
			}
			else
				opdata->state = operation_state::operation_state_t(opdata->state - 2); // setup retry

			internal_db_.save(opdata); // save before we potentially hand off to another thread

			if(!error)
				convert_done_channel_.produce(opdata);
		}
	}

	void run()
	{
		while(!quit_) {
			try {
				operation_data::pointer opdata = convert_done_channel_.consume();
				const std::string& identifier(opdata->p_detail->identifier);
				if(opdata->state == operation_state::put_file_convert_ok) {

					opdata->state = operation_state::put_file_save;
					s3_operation_channel_.produce(
							storage::s3::operation_ex(
									storage::operation::put_object_commit,
									opdata,
									boost::bind(&dispatcher::queue_for_retention_save, this, _1, _2)));
				}
//				else if(opdata->state == operation_state::get_file_convert_ok) {
//					//TODO
//				}
				else
					LogError(identifier<<": invalid state found; state:"<<opdata->state);
			}
			catch(...) {
			}
		}
	}
};

}  // namespace app

#endif /* DISPATCHER_HPP_ */
