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
	storage::operation_channel_t& operation_channel_;
	operation_data::channel_t& retention_data_channel_;
	// operation_data::channel_t& get_data_ready_channel_;

	database::internal& internal_db_;

	volatile bool& quit_;

public:
	dispatcher(
			operation_data::channel_t& convert_done_channel,
			storage::operation_channel_t& operation_channel,
			operation_data::channel_t& retention_data_channel,
			// operation_data::channel_t& get_data_ready_channel,
			database::internal& internal_db,
			volatile bool& quit) :
				convert_done_channel_(convert_done_channel),
				operation_channel_(operation_channel),
				retention_data_channel_(retention_data_channel),
				// get_data_ready_channel_(get_data_ready_channel),
				internal_db_(internal_db),
				quit_(quit)
	{
	}

	void queue_for_retention_save(operation_data::pointer opdata, const storage::operation_result::operation_result_t& storage_opr_result)
	{
		opdata->hibernate();

		if(storage_opr_result == storage::operation_result::success) {
			LogMessage(opdata->p_detail->identifier<<": Saved to S3; queueing for retention");
			opdata->p_detail->state = operation_state::make(opdata->p_detail->state, operation_outcome::ok);
			internal_db_.save(opdata); // save before we potentially hand off to another thread
			retention_data_channel_.produce(opdata);
		}
		else {
			bool error = false;
			LogWarning(opdata->p_detail->identifier<<": Saving to S3 failed");
			opdata->p_detail->retry++;
			if(opdata->p_detail->retry > 3) { // max retry reached; mark error
				error = true;
				opdata->p_detail->state = operation_state::make(opdata->p_detail->state, operation_outcome::error);
				LogError(opdata->p_detail->identifier<<": Failed store after "<<int(opdata->p_detail->retry)<<" retry(s)");
			}
			else
				opdata->p_detail->state = operation_state::put_file_convert_ok; // setup retry

			internal_db_.save(opdata); // save before we potentially hand off to another thread

			if(!error) {
				if (!convert_done_channel_.produce(opdata))
					LogWarning(opdata->p_detail->identifier<<": setup of retry failed");
			}
			else
				LogError(opdata->p_detail->identifier<<": retry > 3; discarding...");
		}
	}

	void run()
	{
		while(!quit_) {
			try {
				operation_data::pointer opdata = convert_done_channel_.consume();

				const operation::operation_t& arcd_operation(operation::operation_t((opdata->p_detail->state & 0x0f00) >> 8));

				switch(arcd_operation) {
				case operation::put_object:
					if((operation_state::put_file_save_ok == opdata->p_detail->state) || (((opdata->p_detail->state & 0x00f0) >> 4) > app::operation_coarse::file_save)) {
						LogVerbose(opdata->p_detail->identifier<<" already saved; shifting to next channel.");
						queue_for_retention_save(opdata, storage::operation_result::success);
						continue;
					}

					if(opdata->p_detail->state == operation_state::put_file_convert_ok || opdata->p_detail->state == operation_state::put_file_save) {
						opdata->p_detail->state = operation_state::put_file_save;
						operation_channel_.produce(
								storage::operation_ex(
										app::operation::put_object,
										opdata,
										boost::bind(&dispatcher::queue_for_retention_save, this, _1, _2)));
					}
					break;

				default:
					LogCritical("Not implemented; operation:"<<arcd_operation);
					break;
				}
			}
			catch(...) {
			}
		}
	}
};

}  // namespace app

#endif /* DISPATCHER_HPP_ */
