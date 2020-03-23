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
#include "monitor.hpp"

#include <boost/bind.hpp>

namespace app {

class dispatcher
{
	operation_data::channel_t& convert_done_channel_;
	storage::operation_channel_t& operation_channel_;
	operation_data::channel_t& retention_data_channel_;
	// operation_data::channel_t& get_data_ready_channel_;

	database::internal& internal_db_;

	storage::cached_endpoint_finder_t& cached_endpoint_finder_;

	volatile bool& quit_;

public:
	dispatcher(
			operation_data::channel_t& convert_done_channel,
			storage::operation_channel_t& operation_channel,
			operation_data::channel_t& retention_data_channel,
			// operation_data::channel_t& get_data_ready_channel,
			database::internal& internal_db,
			storage::cached_endpoint_finder_t& cached_endpoint_finder,
			volatile bool& quit) :
				convert_done_channel_(convert_done_channel),
				operation_channel_(operation_channel),
				retention_data_channel_(retention_data_channel),
				// get_data_ready_channel_(get_data_ready_channel),
				internal_db_(internal_db),
				cached_endpoint_finder_(cached_endpoint_finder),
				quit_(quit)
	{
	}

	void queue_for_retention_save(operation_data::pointer opdata, const storage::operation_result::operation_result_t& storage_opr_result)
	{
		opdata->hibernate();

		if(storage_opr_result == storage::operation_result::success) {
			LogMessage(opdata->p_detail->identifier<<": Saved to storage; queueing for retention");
			opdata->p_detail->state = operation_state::make(opdata->p_detail->state, operation_outcome::ok);
			internal_db_.save(opdata); // save before we potentially hand off to another thread
			pc_produce(retention_data_channel_, opdata);
			
			monitor::increment_stored();
			monitor::increment_stored_size(opdata->p_detail->real_size);
		}
		else {
			opdata->p_detail->retry++;
			LogMessage(opdata->p_detail->identifier<<": Saving to storage failed; setting up retry");
			opdata->p_detail->state = operation_state::put_file_convert_ok; // setup retry

			internal_db_.save(opdata); // save before we potentially hand off to another thread

			// give configuration a chance to change!
			// we will look it up before we retry
			opdata->p_detail->p_endpoint.reset();
			
			bool ret(false);
			// exponential retry ... 3 min, 12 mins, 27 mins, ... after 5 we retry every 2 hours
			pc_produce_get_result(ret, convert_done_channel_, opdata, ((opdata->p_detail->retry > 5)? 7200: (180 * opdata->p_detail->retry * opdata->p_detail->retry)), 0, 0);
			if (!ret)
				LogError(opdata->p_detail->identifier<<": setup of retry failed");
		}
	}

	void run()
	{
		while(!quit_) {
			try {
				operation_data::pointer opdata = convert_done_channel_.consume(10);

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
						
						if(!opdata->p_detail->p_endpoint) {
							const std::pair<archiving::org_id_t, archiving::endpoint_id_t>& lookup(
									std::make_pair<archiving::org_id_t, archiving::endpoint_id_t>(
											opdata->p_detail->identifier.org, opdata->p_detail->policy.p_policy->endpoint_id));
							if(!(opdata->p_detail->p_endpoint = cached_endpoint_finder_(lookup))) {
								cached_endpoint_finder_.forget(lookup);
								THROW_RML_EXCEPTION("Endpoint not found "<<lookup.first<<'/'<<lookup.second);
							}
						}
						
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
			catch(const operation_data::channel_t::finishing_exception& e) {
				LogVerbose(e.what());
				break;
			}
			catch(const operation_data::channel_t::consume_timed_out_exception& e) {
				LogDebug("Timed-out on items to dispatch");
			}
			catch(const std::exception& e) {
				LogWarning("Exception: "<<e.what());
			}
		}
	}
};

}  // namespace app

#endif /* DISPATCHER_HPP_ */
