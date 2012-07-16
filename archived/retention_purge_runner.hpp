/*
 * retention_purge.hpp
 *
 *  Created on: 9 Feb 2012
 *      Author: dkorah
 */

#ifndef RETENTION_PURGE_HPP_
#define RETENTION_PURGE_HPP_

#include "monitor.hpp"
#include "retention_record.hpp"
#include "state_reactor.hpp"
#include "retention_purge_batch.hpp"
#include "./database_retention.hpp"
#include "./database_internal.hpp"

#include <rml/pc_channel.h>

#include <boost/enable_shared_from_this.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/date_time/period.hpp>

namespace app {

namespace retention_purge {

/**
 * retention policy based purge
 * Here we identify the purge set and feed them into storage operations channel
 */
class runner : public boost::enable_shared_from_this<runner>
{
	const boost::program_options::variables_map& confMap_;

	const storage::cached_endpoint_finder_t& storage_endpoint_finder_;

	operation_data::channel_t& purge_done_channel_;
	state_reactor& reactor_;
	database::retention& retention_db_;
	database::internal& internal_db_;

	enum
	{
		max_limit_org = 1000,
		min_limit_org = 1,

		max_limit_object = 1000,
		min_limit_object = 10,

		put_rate_cut_off = 1000,
		put_rate_zero = 50,

		purge_batch_days = 7,

		purge_min_queue_size = 5000
	};

	volatile bool& quit_;

	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("retention_purge.require_approval",  boost::program_options::value<bool>()->default_value(true), "Require approval before real purge")
				;
	}

	runner(
			const boost::program_options::variables_map& confMap,
			const storage::cached_endpoint_finder_t& storage_endpoint_finder, operation_data::channel_t& purge_done_channel, state_reactor& reactor,
			database::retention& retention_db, database::internal& internal_db, volatile bool& quit) :
				confMap_(confMap),
				storage_endpoint_finder_(storage_endpoint_finder), purge_done_channel_(purge_done_channel), reactor_(reactor),
				retention_db_(retention_db), internal_db_(internal_db), quit_(quit)
	{
	}

	/**
	 * enqueue purge
	 *
	 * @param org_id
	 * @param last_purge_retention_max_date
	 * @param purge_batch_days
	 *
	 * @return bool true if done with the current date range
	 */
	bool batch_purge(const archiving::org_id_t& org_id, const applied_policy::ymd_t& last_purge_retention_max_date, const unsigned int purge_batch_days, const unsigned int purge_batch_count)
	{
		bool move_forward = false;
		boost::date_time::date max_date( boost::date_time::date(last_purge_retention_max_date) + boost::date_time::date_duration(purge_batch_days) );

		const retention_record::set_t& purgeables( retention_db_.get_retention(org_id, last_purge_retention_max_date, max_date.year_month_day(), purge_batch_count) );
		move_forward = purgeables.size() != purge_batch_count;

		for(retention_record::set_t::const_iterator itr = purgeables.begin(); itr != purgeables.end(); ++itr) {
			storage::basic_endpoint::pointer endpoint = storage_endpoint_finder_(
					std::make_pair< archiving::org_id_t, archiving::endpoint_id_t >((*itr)->identifier.org, (*itr)->endpoint_id));
			if(!endpoint) {
				storage_endpoint_finder_.forget(
						std::make_pair< archiving::org_id_t, archiving::endpoint_id_t >((*itr)->identifier.org, (*itr)->endpoint_id));
				LogWarning((*itr)->identifier<<" could not find storage endpoint:"<<(*itr)->endpoint_id);
				continue;
			}

			operation_data::pointer opdata = boost::make_shared<operation_data>(boost::make_shared<operation_data::detail>());
//			operation_data::pointer opdata(new operation_data(operation_data::detail::pointer(new operation_data::detail())));

			opdata->p_detail->identifier = (*itr)->identifier;
			opdata->p_detail->p_endpoint = endpoint;

			opdata->p_detail->arrival_time = time(0);
			opdata->p_detail->current_file_format = (*itr)->file_format;
			opdata->p_detail->target_file_format = (*itr)->file_format;
			opdata->p_detail->state = operation_state::purge_init;

			reactor_.schedule(
					boost::bind(&runner::purge_initialise, shared_from_this(), opdata),
					boost::bind(&runner::move_forward, shared_from_this(), opdata, _1),
					state_reactor::very_low, false);
		}


		// make operation_data
		// Get Clear Size and Real Size and populate into operation_data
		//

		return move_forward;
	}

	bool purge_initialise(operation_data::pointer opdata)
	{
		storage::s3::backend::pointer storage_backend = storage::s3::backend::get_instance();

		storage::string_map_t props;
		storage_backend->stat(opdata, props);
		if(props.size()) {
			if(props.find("clear-size") != props.end())
				opdata->p_detail->clear_size = rml::utils::strtoull(props["clear-size"].c_str(), 0ULL);

			if(props.find("real-size") != props.end())
				opdata->p_detail->real_size = rml::utils::strtoull(props["real-size"].c_str(), 0ULL);
		}
		else
			LogWarning((*itr)->identifier<<" could not stat item on endpoint:"<<(*itr)->endpoint_id);

		return true;
	}

	void move_forward(operation_data::pointer opdata, const bool& outcome_ok)
	{
		if (outcome_ok) {
			opdata->p_detail->state = operation_state::next_up(opdata->p_detail->state);
			const operation_coarse::operation_coarse_t& operation((opdata->p_detail->state & operation_coarse::mask) >> 4);
			switch(operation) {

			case operation_coarse::approve_purge:
				if (confMap_["retention_purge.require_approval"].as<bool>()) {
					reactor_.schedule(
							boost::bind(&runner::purge_initialise, shared_from_this(), opdata),
							boost::bind(&runner::move_forward, shared_from_this(), opdata, _1),
							state_reactor::very_low, false);
					break;
				}

			case operation_coarse::file_purge:
					reactor_.schedule(
							boost::bind(&runner::purge_initialise, shared_from_this(), opdata),
							boost::bind(&runner::move_forward, shared_from_this(), opdata, _1),
							state_reactor::very_low, false);
					break;

			case operation_coarse::usage_purge:
				reactor_.schedule(
						boost::bind(&runner::purge_initialise, shared_from_this(), opdata),
						boost::bind(&runner::move_forward, shared_from_this(), opdata, _1),
						state_reactor::very_low, false);
				break;

			case operation_coarse::runner:
				reactor_.schedule(
						boost::bind(&runner::purge_initialise, shared_from_this(), opdata),
						boost::bind(&runner::move_forward, shared_from_this(), opdata, _1),
						state_reactor::very_low, false);
				break;

			default:
				LogCritical("Unknown state:"<<opdata->p_detail->state<<" cannot move forward");
				break;
			}
		}
		else {
			// TODO handle errors mid way!!!
			opdata->p_detail->state = operation_state::make(opdata->p_detail->state, outcome_ok);
			LogWarning(opdata<<" Failed");
		}
	}

	int get_average_put_rate()
	{
		static std::vector< size_t > samples;

		if(samples.size() >= 5)
			for(int i = 0; i < 4; i++)
				samples[i] = samples[i+1];

		samples[samples.size() >= 5? 5: samples.size()] = monitor::get_counter().put;

		if(samples.size() < 5)
			return -1;

		size_t sum = 0;
		for(int i = 0; i < 5; i++)
			sum += samples[i];

		return sum / 5;
	}

	/**
	 * find records from Retention DB and pool them into Purge Queue
	 */
	void feed_purge_queue()
	{
		int iter = 1;

		types::purge_def::pointer p_purge_def;

		while(!quit_) {
			if(iter++ % 60) {
				boost::this_thread::sleep(boost::posix_time::seconds(1));
				continue;
			}

			try {
				// check if we need to feed the queue
				if (purge_min_queue_size < internal_db_.get_purge_queue_size(purge_min_queue_size))
					continue;

				int average_put_rate_5mins = get_average_put_rate();
				if(average_put_rate_5mins < 0) {
					LogMessage("Not enough samples for purge rate-limiting; continuing to wait...");
					continue;
				}

				if(average_put_rate_5mins >= put_rate_cut_off) {
					LogMessage("Purge rate-limiting: average PUT rate (5mins) found above cut-off rate [ "<<average_put_rate_5mins<<" >= "<<put_rate_cut_off<<" ]");
					continue;
				}

				if(!p_purge_def) {
					LogDebug("First loading of purge def!");
					p_purge_def = retention_db_.get_orgs_for_purge();
				}

				const unsigned int limit( min_limit_org + ((max_limit_org - min_limit_org) * (average_put_rate_5mins - put_rate_zero) / (put_rate_cut_off - put_rate_zero)) );

				p_purge_def = internal_db_.update_get_purge_def(p_purge_def, limit);

				if (!p_purge_def->set.empty()) {
					const types::purge_def::set_t::const_iterator& end(p_purge_def->set.end());
					for(types::purge_def::set_t::const_iterator itr = p_purge_def->set.begin(); itr != end; ++itr) {
						batch_purge(itr->org_id, itr->last_purge_retention_max_date, purge_batch_days);
					}
				}
			}
			catch(const std::exception& e) {
				LogWarning(e.what());
			}
		}
	}
};

}  // namespace retention_purge

}  // namespace app

#endif /* RETENTION_PURGE_HPP_ */
