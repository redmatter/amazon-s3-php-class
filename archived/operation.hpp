/*
 * operation.hpp
 *
 *  Created on: 21 Feb 2011
 *      Author: dkorah
 */

#ifndef OPERATION_HPP_
#define OPERATION_HPP_

#include "operation_data.hpp"
#include "storage_endpoint.hpp"
#include "policy.hpp"

#include <rml/pc_channel.h>

namespace app {

typedef rml::pc_channel< operation_data::pointer, operation_data::pointer_compare_lt > operation_data_channel_t;

class operation
{
	volatile const bool& quit_;
	operation_data_channel_t& channel_;

	storage::cached_endpoint_finder_t& cached_endpoint_finder_;
	cached_policy_finder_t& cached_policy_finder_;
	cached_policy_acl_finder_t& cached_policy_acl_finder_;

public:
	operation(
			operation_data_channel_t& channel,
			volatile const bool& quit,
			storage::cached_endpoint_finder_t& cached_endpoint_finder,
			cached_policy_finder_t& cached_policy_finder,
			cached_policy_acl_finder_t& cached_policy_acl_finder
			) :
		quit_(quit),
		channel_(channel),
		cached_endpoint_finder_(cached_endpoint_finder),
		cached_policy_finder_(cached_policy_finder),
		cached_policy_acl_finder_(cached_policy_acl_finder)
	{
		//ctor
	}

	void run()
	{
		while(!quit_) {
			operation_data::pointer opdata = channel_.consume();
			LogVerbose(channel_<<"Got: "<<opdata->detail->identifier);

			switch(opdata->state) {

			case operation_state::init:
			{
				throw rml::exception("Invalid state 'init'");
				break;
			}

			case operation_state::put_received:
			{
				assert(opdata->p_detail->p_metadata.get());
				assert(opdata->p_detail->org_id);
				assert(opdata->p_detail->p_policy.get());
				assert(opdata->file_type != archiving::archivable::file_type::unknown);

				storage::s3::endpoint::pointer endpoint = cached_endpoint_finder_(opdata->org_id);
				// TODO set p_metadata ( retention ) based on Policy

				assert(opdata->current_file_format);


			}

			}
		}
	}
};

}  // namespace app

#endif /* OPERATION_HPP_ */
