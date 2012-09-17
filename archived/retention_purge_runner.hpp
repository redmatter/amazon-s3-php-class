/*
 * retention_purge.hpp
 *
 *  Created on: 9 Feb 2012
 *      Author: dkorah
 */

#ifndef RETENTION_PURGE_RUNNER_HPP_
#define RETENTION_PURGE_RUNNER_HPP_

#include "monitor.hpp"
#include "retention_record.hpp"
#include "state_reactor.hpp"
#include "retention_purge.hpp"
#include "./database_retention.hpp"
#include "./database_internal.hpp"

#include "../common/backtrace.hpp"

#include <rml/pc_channel.h>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/date_time/period.hpp>
#include <boost/date_time/gregorian/gregorian_types.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <math.h>

namespace {
struct constants
{
	enum
	{
		db_retry_count = 2,
		db_retry_factor = 5
	};
};
}

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

	state_reactor<>& reactor_;
	database::retention& retention_db_;
	database::internal& internal_db_;

	item::channel_t approval_save_channel_;
	item::channel_t delete_file_channel_;
	item::channel_t delete_usage_channel_;
	item::channel_t delete_retention_channel_;
	item::channel_t finished_channel_;

	time_t last_delete_file_time_;
	time_t last_delete_usage_time_;
	time_t last_delete_retention_time_;
	time_t purge_finished_time_;

	boost::mutex last_delete_file_time_mutex_;
	boost::mutex last_delete_usage_time_mutex_;
	boost::mutex last_delete_retention_time_mutex_;
	boost::mutex purge_finished_time_mutex_;

	volatile bool& quit_;

	enum
	{
		max_limit_object = 10240,
		min_limit_object = 32,

		put_rate_cut_off = 1024,
		put_rate_zero = 64,

		purge_batch_days = 7,
		purge_batch_count = 1000,

		purge_min_queue_size = 5000,

		purge_file_interval_seconds = 10,
		purge_usage_interval_seconds = 20,
		purge_retention_interval_seconds = 20,
		purge_finished_interval_seconds = 20
	};

public:
	runner(
			const boost::program_options::variables_map& confMap,
			const storage::cached_endpoint_finder_t& storage_endpoint_finder, state_reactor<>& reactor,
			database::retention& retention_db, database::internal& internal_db, volatile bool& quit) :
				confMap_(confMap),
				storage_endpoint_finder_(storage_endpoint_finder), reactor_(reactor),
				retention_db_(retention_db), internal_db_(internal_db),
				approval_save_channel_("SaveApproval"), delete_file_channel_("DeleteFile"),
				delete_usage_channel_("DeleteUsage"), delete_retention_channel_("DeleteRetention"), finished_channel_("Finished"),
				last_delete_file_time_(time(0)), last_delete_usage_time_(last_delete_file_time_), purge_finished_time_(last_delete_file_time_),
				quit_(quit)
	{
	}

	/**
	 * enqueue purge
	 *
	 * @return new max id
	 */
	uint64_t batch_purge(const archiving::org_id_t& org_id, const uint64_t& max_retention_id, const unsigned int batch_count)
	{
		uint64_t new_max_retention_id(max_retention_id);
		const retention_record::set_t& purgeables( retention_db_.get_retention_for_purge(org_id, max_retention_id, batch_count, confMap_["retention_purge.lag_days"].as<unsigned int>()) );
		for(retention_record::set_t::const_iterator itr = purgeables.begin(); itr != purgeables.end(); ++itr) {
			storage::basic_endpoint::pointer endpoint = storage_endpoint_finder_(
					std::make_pair< archiving::org_id_t, archiving::endpoint_id_t >((*itr)->identifier.org, (*itr)->endpoint_id));
			if(!endpoint) {
				storage_endpoint_finder_.forget(
						std::make_pair< archiving::org_id_t, archiving::endpoint_id_t >((*itr)->identifier.org, (*itr)->endpoint_id));
				LogWarning("Purge:"<<(*itr)->identifier<<" could not find storage endpoint:"<<(*itr)->endpoint_id);
				continue;
			}

			if((*itr)->id > new_max_retention_id)
				new_max_retention_id = (*itr)->id;

			item::pointer purge_item = boost::make_shared< item >(
					(*itr)->identifier, endpoint, *itr);

			reactor_.schedule(
					boost::bind(&runner::purge_initialise, shared_from_this(), purge_item),
					boost::bind(&runner::move_forward, shared_from_this(), purge_item, _1),
					state_reactor<>::very_low, false);
		}

		return new_max_retention_id;
	}

	bool purge_initialise(item::pointer p_item) try
	{
		storage::s3::backend::pointer storage_backend = storage::s3::backend::get_instance();

		storage::string_map_t props;
		boost::static_pointer_cast<storage::s3::backend>(storage_backend)->stat(
				p_item->endpoint, p_item->identifier, props);
		LogDebug("stat("<<p_item->identifier<<") "<<props);

		if(!props.size())
			THROW_RML_EXCEPTION("could not stat item on endpoint:"<<p_item->endpoint->id);

		if(props.find("x-amz-meta-clear-size") != props.end())
			p_item->clear_size = rml::utils::strtoull(props["clear-size"].c_str(), 0ULL);

		if(props.find("content-length") != props.end())
			p_item->real_size = rml::utils::strtoull(props["content-length"].c_str(), 0ULL);

#define AssertProps(key) \
		if(props.find("x-amz-meta-"key) == props.end()) throw rml::exception(key" not found in HEAD response")

		AssertProps("policy-id");
		AssertProps("policy-version");
		AssertProps("creation-date");
		AssertProps("retention-min-date");
		AssertProps("retention-max-date");

#undef AssertProps

		const retention_record r(
				p_item->identifier,
				p_item->retention->file_format,
				p_item->retention->user_id,
				rml::utils::strtoll(props["x-amz-meta-policy-id"].c_str(), 0LL),
				rml::utils::strtoull(props["x-amz-meta-policy-version"].c_str(), 0ULL),
				boost::gregorian::from_simple_string(props["x-amz-meta-creation-date"]).year_month_day(),
				boost::gregorian::from_simple_string(props["x-amz-meta-retention-min-date"]).year_month_day(),
				boost::gregorian::from_simple_string(props["x-amz-meta-retention-max-date"]).year_month_day(),
				0,
				false,
				true,
				p_item->endpoint->id,
				p_item->retention->id);
		if(*(p_item->retention) != r) {
			LogDebug("database: "<<p_item->retention);
			LogDebug("s3-metadata: "<<r);
			throw rml::exception("Retention data mismatch (s3-metadata != database)");
		}

		return true;
	}
	catch(const std::exception& e) {
		LogWarning(p_item->identifier<<" Failed to initialise; err:"<<e.what());
		return false;
	}

	// dummy function to move the item through the state-reactor
	inline bool purge_stage_done(item::pointer p_item)
	{
		return true;
	}

	/**
	 * After a restart, we need to start back from where we left off
	 */
	void reload_from_database()
	{
//		static uint64_t max_loaded(0);
//
//		while(!quit_) {
//			internal_db_.reload_purge_queue(max_loaded)
//		}
	}

	void approval_run()
	{
		if(!confMap_["retention_purge.require_approval"].as<bool>()) {
			LogDWarning("Approval not required; quitting thread", "IGNO");
			return;
		}

		while(!quit_) {
			// TODO load approved items

			// we keep approval-required items till we save them on the db thread, in the approval_save_channel
			// TODO we need to consume then and check if saved?!
		}
	}

	void move_forward(item::pointer p_item, const bool& outcome_ok)
	{
		p_item->cur_state = state::make(p_item->cur_state, outcome_ok);
		internal_db_.batch_save< item >(p_item);

		if (outcome_ok) {
			p_item->cur_state = state::next_up(p_item->cur_state);
			const state::coarse_t& coarse_state(state::get_coarse(p_item->cur_state));

			const bool& require_approval(confMap_["retention_purge.require_approval"].as<bool>());
			if(coarse_state == state::_approve_ && !require_approval) {
				// if we need no approval; skip this stage
				p_item->cur_state = state::make(
						state::next_up(p_item->cur_state), true);
			}

			switch(coarse_state) {

			case state::_approve_:
			{
				assert(require_approval);

				LogDebug(p_item<<" Keeping in approval_save_channel_ for saving");

				// we keep approval-required items till we save them on the db thread, in the approval_save_channel
				pc_produce(approval_save_channel_, p_item, 10 + confMap_["retention_purge.db_flush_interval_seconds"].as<unsigned int>(), 0, 0);

				try {
					// consume and discard if saved
					item::pointer pi = approval_save_channel_.consume(0, 100);
					if(!item::is_saved(pi)) {
						LogWarning(pi<<" BACK into approval_save_channel_ for saving");
						pc_produce(approval_save_channel_, pi, 10 + confMap_["retention_purge.db_flush_interval_seconds"].as<unsigned int>(), 0, 0);
					}
				}
				catch(const item::channel_t::consume_timed_out_exception& e) {
				}
				catch(const item::channel_t::finishing_exception& e) {
				}
				break;
			}

			case state::_delete_file_:
			{
				pc_produce(delete_file_channel_, p_item);

				if((time(0) - last_delete_file_time_) < purge_file_interval_seconds)
					break;

				try {
					boost::mutex::scoped_lock lock(last_delete_file_time_mutex_, boost::try_to_lock);
					if(lock) {
						last_delete_file_time_ = time(0);

						item::endpoint_grouped_multiset_t purge_set;

						unsigned int batch_size(1000);
						while(!quit_ && batch_size--) {
							try {
								purge_set.insert(delete_file_channel_.consume(0, 500000));
							}
							catch(const item::channel_t::consume_timed_out_exception& e) {
								break;
							}
							catch(const item::channel_t::finishing_exception& e) {
								break;
							}
						}

						// we could get here if quit was flagged; if so we need to quit asap
						if(!quit_ && !purge_set.empty()) {
							item::endpoint_grouped_multiset_t::const_iterator itr = purge_set.begin();
							boost::shared_ptr< storage::s3::backend > storage_backend =
									boost::static_pointer_cast< storage::s3::backend >(
											storage::s3::backend::get_instance());
							do {
								// find a batch
								const std::pair<
										item::endpoint_grouped_multiset_t::const_iterator,
										item::endpoint_grouped_multiset_t::const_iterator>& itr_batch(
												purge_set.equal_range(*itr));

								// perform multiple-delete
								storage_backend->remove_multiple< item::endpoint_grouped_multiset_t::const_iterator,
										item::extract_endpoint, item::extract_identifier >(
												itr_batch.first, itr_batch.second);

								for(item::endpoint_grouped_multiset_t::const_iterator itr_ = itr_batch.first; itr_ != itr_batch.second; ++itr_)
									// route it via the reactor so that we can distribute work over threads, rather than moving forward with this thread
									reactor_.schedule(
											boost::bind(&runner::purge_stage_done, shared_from_this(), *itr_),
											boost::bind(&runner::move_forward, shared_from_this(), *itr_, _1),
											state_reactor<>::low, false);

								// move a batch forward
								itr = itr_batch.second;
							} while(itr != purge_set.end());
						}

						// to adjust for delays from processing the batch
						last_delete_file_time_ = time(0);
					}
				}
				catch(const boost::lock_error& e) {
					LogVerbose("Failed to gain file-purge-lock; skipping... [exception:"<<e.what()<<']');
				}

				break;
			}

			case state::_delete_usage_:
			{
				pc_produce(delete_usage_channel_, p_item);

				if((time(0) - last_delete_usage_time_) < purge_usage_interval_seconds)
					break;

				try {
					boost::mutex::scoped_lock lock(last_delete_usage_time_mutex_, boost::try_to_lock);
					if(lock) {
						last_delete_usage_time_ = time(0);

						item::type_org_endpoint_grouped_multiset_t purge_set;

						unsigned int batch_size(2000);
						while(!quit_ && batch_size--) {
							try {
								purge_set.insert(delete_usage_channel_.consume(0, 500000));
							}
							catch(const item::channel_t::consume_timed_out_exception& e) {
								break;
							}
							catch(const item::channel_t::finishing_exception& e) {
								break;
							}
						}

						// we could get here if quit was flagged; if so we need to quit asap
						if(!quit_ && !purge_set.empty()) {
							item::type_org_endpoint_grouped_multiset_t::const_iterator itr = purge_set.begin();
							do {
								const std::pair<
										item::type_org_endpoint_grouped_multiset_t::const_iterator,
										item::type_org_endpoint_grouped_multiset_t::const_iterator>& itr_batch(
												purge_set.equal_range(*itr));

								int count(0);
								int64_t clear_usage(0), real_usage(0);
								for(item::type_org_endpoint_grouped_multiset_t::const_iterator itr_ = itr_batch.first; itr_ != itr_batch.second; ++itr_) {
									count--;
									clear_usage -= (*itr_)->clear_size;
									real_usage -= (*itr_)->real_size;
								}

								bool done_ok(false);
								unsigned int retry(constants::db_retry_count);
								while(!done_ok && retry--) {
									if(retry < (constants::db_retry_count - 1)) {
										if(!retry)
											LogWarning("SQL failed; last attempt in a few minutes");
										else
											LogMessage("SQL failed; retrying in a few seconds");

										int sleep_seconds((int)pow(constants::db_retry_factor, constants::db_retry_count - retry));
										while(!quit_ && sleep_seconds--)
											boost::this_thread::sleep(boost::posix_time::seconds(1));
									}

									if(!quit_)
										done_ok = retention_db_.purge_usage(
												(*itr)->identifier.org, (*itr)->identifier.filetype, (*itr)->endpoint->id,
												count, clear_usage, real_usage);
								}

								for(item::type_org_endpoint_grouped_multiset_t::const_iterator itr_ = itr_batch.first;
										itr_ != itr_batch.second; ++itr_) {
									// if done ok, move forward
									if(done_ok)
										reactor_.schedule(
												boost::bind(&runner::purge_stage_done, shared_from_this(), *itr_),
												boost::bind(&runner::move_forward, shared_from_this(), *itr_, _1),
												state_reactor<>::low, false);
									// else if we are not about to quit, enqueue for retry aftre 10 mins
									else if(!quit_)
										pc_produce(delete_usage_channel_, p_item, 600, 0, 0);
								}

								itr = itr_batch.second;
							} while(!quit_ && (itr != purge_set.end()));
						}

						// to adjust for delays from processing the batch
						last_delete_usage_time_ = time(0);
					}
				}
				catch(const boost::lock_error& e) {
					LogVerbose("Failed to gain purge-usage-lock; skipping... [exception:"<<e.what()<<']');
				}

				break;
			}

			case state::_delete_retention_:
			{
				pc_produce(delete_retention_channel_, p_item);

				if((time(0) - last_delete_retention_time_) < purge_retention_interval_seconds)
					break;

				try {
					boost::mutex::scoped_lock lock(last_delete_retention_time_mutex_, boost::try_to_lock);
					if(lock) {
						last_delete_retention_time_ = time(0);

						item::org_grouped_multiset_t purge_set;

						unsigned int batch_size(2000);
						while(!quit_ && batch_size--) {
							try {
								purge_set.insert(delete_retention_channel_.consume(0, 500000));
							}
							catch(const item::channel_t::consume_timed_out_exception& e) {
								break;
							}
							catch(const item::channel_t::finishing_exception& e) {
								break;
							}
						}

						// we could get here if quit was flagged; if so we need to quit asap
						if(!quit_ && !purge_set.empty()) {
							item::org_grouped_multiset_t::const_iterator itr = purge_set.begin();
							do {
								const std::pair<
										item::org_grouped_multiset_t::const_iterator,
										item::org_grouped_multiset_t::const_iterator>& itr_batch(
												purge_set.equal_range(*itr));

								bool done_ok(false);
								unsigned int retry(constants::db_retry_count);
								while(!done_ok && retry--) {
									if(retry < (constants::db_retry_count - 1)) {
										if(!retry)
											LogWarning("SQL failed; last attempt in a few minutes");
										else
											LogMessage("SQL failed; retrying in a few seconds");

										int sleep_seconds((int)pow(constants::db_retry_factor, constants::db_retry_count - retry));
										while(!quit_ && sleep_seconds--)
											boost::this_thread::sleep(boost::posix_time::seconds(1));
									}

									if(!quit_)
										done_ok = retention_db_.set_purged<
												item::org_grouped_multiset_t::const_iterator,
												item::extract_retention_id >(
														(*itr_batch.first)->identifier.org, itr_batch.first, itr_batch.second);
								}

								for(item::org_grouped_multiset_t::const_iterator itr_ = itr_batch.first;
										itr_ != itr_batch.second; ++itr_) {
									// if done ok, move forward
									if(done_ok)
										reactor_.schedule(
												boost::bind(&runner::purge_stage_done, shared_from_this(), *itr_),
												boost::bind(&runner::move_forward, shared_from_this(), *itr_, _1),
												state_reactor<>::low, false);
									// else if we are not about to quit, enqueue for retry aftre 10 mins
									else if(!quit_)
										pc_produce(delete_retention_channel_, p_item, 600, 0, 0);
								}

								itr = itr_batch.second;
							} while(itr != purge_set.end());
						}

						// to adjust for delays from processing the batch
						last_delete_retention_time_ = time(0);
					}
				}
				catch(const boost::lock_error& e) {
					LogVerbose("Failed to gain purge-retention-lock; skipping... [exception:"<<e.what()<<']');
				}

				break;
			}

			case state::_finished_:
			{
				pc_produce(finished_channel_, p_item, 120, 0, 0);

				if((time(0) - purge_finished_time_) < purge_finished_interval_seconds)
					break;

				try {
					boost::mutex::scoped_lock lock(purge_finished_time_mutex_, boost::try_to_lock);
					if(lock) {
						purge_finished_time_ = time(0);

						item::list_t purge_list;

						unsigned int batch_size(2000);
						while(!quit_ && batch_size--) {
							try {
								purge_list.push_back(finished_channel_.consume(0, 500000));
							}
							catch(const item::channel_t::consume_timed_out_exception& e) {
								break;
							}
							catch(const item::channel_t::finishing_exception& e) {
								break;
							}
						}

						internal_db_.delete_by_ids< item, item::list_t::const_iterator, item::extract_id >(
										purge_list.begin(), purge_list.end(), "ArchiveDRetentionPurgeQueue", "ID");

						// to adjust for delays from processing the batch
						purge_finished_time_ = time(0);
					}
				}
				catch(const boost::lock_error& e) {
					LogVerbose("Failed to gain file-purge-lock; skipping... [exception:"<<e.what()<<']');
				}

				break;
			}

			default:
				LogCritical(p_item<<" Unknown state; cannot move forward");
				break;
			}
		}
		else {
			// TODO handle errors mid way!!!
			LogWarning(p_item<<" Failed");
		}
	}

	enum
	{
		put_rate_sample_size = 2
	};

	int get_average_put_rate()
	{
		static std::list< size_t > samples;

		while(samples.size() >= put_rate_sample_size)
			samples.pop_back();

		samples.push_front(monitor::get_counter().put);

		if(samples.size() < put_rate_sample_size)
			return -1;

		size_t sum = 0;
		for(std::list< size_t >::const_iterator i = samples.begin(); i != samples.end(); ++i)
			sum += *i;

		return sum / put_rate_sample_size;
	}

	/**
	 * find records from Retention DB and pool them into Purge Queue
	 */
	void feed_purge_queue()
	{
		int iter = 1;

		types::purge_def::pointer p_purge_def;

		while(!quit_) {
			if(!quit_ && (iter++ % 60)) {
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

				// compute the limit based on put rate from statistics
				const unsigned int limit(
						average_put_rate_5mins?
								( min_limit_object + (
										(max_limit_object - min_limit_object) * (
												(average_put_rate_5mins - put_rate_zero) / (put_rate_cut_off - put_rate_zero))) )
								: max_limit_object);

				p_purge_def = internal_db_.update_get_purge_def(p_purge_def, limit);

				if (!p_purge_def->set.empty()) {
					types::purge_def::set_t orgs_update;

					const types::purge_def::set_t::const_iterator& end(p_purge_def->set.end());
					for(types::purge_def::set_t::const_iterator itr = p_purge_def->set.begin(); !quit_ && (itr != end); ++itr) {
						uint64_t new_max_id(batch_purge(itr->org_id, itr->max_retention_id, limit));
						if(new_max_id == itr->max_retention_id) {
							LogMessage("Org:"<<itr->org_id<<" no purgables found"<<(itr->max_retention_id? "; resetting to zero": ""));
							new_max_id = 0;
						}

						orgs_update.insert(types::purge_def::org_def(itr->org_id, new_max_id));
					}

					p_purge_def->set.swap(orgs_update);
				}
			}
			catch(const std::exception& e) {
				LogWarning(e.what());
				app::print_backtrace();
			}
		}
	}
};

}  // namespace retention_purge

}  // namespace app

#endif /* RETENTION_PURGE_RUNNER_HPP_ */
