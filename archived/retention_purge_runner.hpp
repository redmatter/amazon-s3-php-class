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
#include "../common/coreapi_eventlog.hpp"
#include "../common/coreapi.hpp"
#include "../common/policy.hpp"

#include <rml/pc_channel.h>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/date_time/period.hpp>
#include <boost/date_time/gregorian/gregorian_types.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/preprocessor/cat.hpp>

#include <math.h>

#define __pc_produce(channel, item, ...) \
	do { \
		bool ret(false); \
		pc_produce_get_result(ret, channel, item, ##__VA_ARGS__); \
		if(!ret) \
			LogError(item<<" Failed to produce into "<<channel); \
	} while(false)

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
 * Retention policy based purge
 * Here we identify the purge set and feed them through the purge statemachine backed
 * by the reactor
 */
class runner : public boost::enable_shared_from_this<runner>
{
	const boost::program_options::variables_map& confMap_;

	const storage::cached_endpoint_finder_t& storage_endpoint_finder_;
	const policy::cached_finder_t& cached_policy_finder_;
	coreapi& api_;

	state_reactor<>& reactor_;
	database::retention& retention_db_;
	database::internal& internal_db_;

	item::channel_t item_save_channel_;
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

	bool auto_expire_filetype_[archiving::file_type::n_file_types];

	volatile bool& quit_;

	enum
	{
		max_limit_object = 10240,
		min_limit_object = 32,

		put_rate_cut_off = 1024,
		put_rate_zero = 64,

		purge_batch_days = 7,
		purge_batch_count = 1000,

		purge_max_queue_size = 5000,

		purge_file_interval_seconds = 10,
		purge_usage_interval_seconds = 15,
		purge_retention_interval_seconds = 15,
		purge_finished_interval_seconds = 5,

		purge_file_batch_size = 1000,
		purge_usage_batch_size = 2000,
		purge_retention_batch_size = 2000,
		purge_finished_batch_size = 500
	};

public:
	runner(
			const boost::program_options::variables_map& confMap,
			const storage::cached_endpoint_finder_t& storage_endpoint_finder,
			const policy::cached_finder_t& cached_policy_finder,
			coreapi& api, state_reactor<>& reactor,
			database::retention& retention_db, database::internal& internal_db, volatile bool& quit) :
				confMap_(confMap),
				storage_endpoint_finder_(storage_endpoint_finder), cached_policy_finder_(cached_policy_finder), api_(api), reactor_(reactor),
				retention_db_(retention_db), internal_db_(internal_db),
				item_save_channel_("Save"), delete_file_channel_("DeleteFile"),
				delete_usage_channel_("DeleteUsage"), delete_retention_channel_("DeleteRetention"), finished_channel_("Finished"),
				last_delete_file_time_(time(0)), last_delete_usage_time_(last_delete_file_time_),
				last_delete_retention_time_(last_delete_file_time_), purge_finished_time_(last_delete_file_time_),
				quit_(quit)
	{
		const std::string& auto_expiring_filetypes(confMap["retention_purge.auto_expiring_filetypes"].as<std::string>());
		if(!auto_expiring_filetypes.empty()) {
			std::vector<std::string> str_filetypes;
			boost::split(str_filetypes, auto_expiring_filetypes, boost::is_any_of(", \t"));
			std::vector<std::string>::const_iterator itr = str_filetypes.begin();
			std::vector<std::string>::const_iterator end = str_filetypes.end();
			for(; itr != end; ++itr)
				auto_expire_filetype_[(int)archiving::file_type::from_string(*itr)] = true;
		}

		// initialise it! not thread-safe
		(void) get_retention_purge_restrictions(confMap);
	}

	/**
	 * Safe shutdown of the runner
	 */
	void shutdown()
	{
#define ConsumeAllAndSave(channel) \
		channel.set_finishing(); \
		const item::channel_t::bare_item_set_t& BOOST_PP_CAT(items, __LINE__)(channel.consume_remaining()); \
		if( ! BOOST_PP_CAT(items, __LINE__).empty()) {\
			LogVerbose("Found "<<BOOST_PP_CAT(items, __LINE__).size()<<" items from "<<channel); \
			for(item::channel_t::bare_item_set_t::const_iterator itr = BOOST_PP_CAT(items, __LINE__).begin(); itr != BOOST_PP_CAT(items, __LINE__).end(); ++itr) { \
				if((*itr)->retention) { \
					internal_db_.batch_save< item >(*itr); \
				} \
			} \
		}

		// stop intermediately and setup save for al except the finished channel
		ConsumeAllAndSave(item_save_channel_)
		ConsumeAllAndSave(delete_file_channel_)
		ConsumeAllAndSave(delete_usage_channel_)
		ConsumeAllAndSave(delete_retention_channel_)

		LogMessage("Saving and terminating retention purge module");

		// save items
		internal_db_.batch_save_run< item, item::weak_set_t >();

		// consume everything from finished_channel and DELETE them
		finished_channel_.set_finishing();
		const item::channel_t::bare_item_set_t& items_finished(finished_channel_.consume_remaining());
		if(!items_finished.empty())
			internal_db_.delete_by_ids< item, item::channel_t::bare_item_set_t::const_iterator, item::extract_id, item::extract_identifier >(
					items_finished.begin(), items_finished.end(), "ArchiveDPurgeQueue", "ID");
	}

	/**
	 * Batch scheduler
	 *
	 * @param purgeables
	 * @param max_retention_id
	 */
	void schedule_purgeables(const retention_record::set_t& purgeables)
	{
		bool quit_seen(false);
		static const unsigned int db_flush_interval_seconds(
				confMap_["retention_purge.db_flush_interval_seconds"].as<unsigned int>());

		for(retention_record::set_t::const_iterator itr = purgeables.begin(); itr != purgeables.end(); ++itr) {
			storage::basic_endpoint::pointer endpoint = storage_endpoint_finder_(
					std::make_pair< archiving::org_id_t, archiving::endpoint_id_t >((*itr)->identifier.org, (*itr)->endpoint_id));
			if(!endpoint) {
				storage_endpoint_finder_.forget(
						std::make_pair< archiving::org_id_t, archiving::endpoint_id_t >((*itr)->identifier.org, (*itr)->endpoint_id));
				LogWarning("Purge:"<<(*itr)->identifier<<" could not find storage endpoint:"<<(*itr)->endpoint_id);
				continue;
			}

			item::pointer purge_item(new item((*itr)->identifier, endpoint, *itr));

			// save and keep it available till the save() fires!
			internal_db_.batch_save< item >(purge_item);
			__pc_produce(item_save_channel_, purge_item, 15 + db_flush_interval_seconds, 0, 0);

			if(quit_) {
				LogIf(!quit_seen, "Scheduling purgeables interrupted by quit; saving state... (may take a while)");
				quit_seen = true;
			}
			else {
				reactor_.schedule(
						boost::bind(&runner::purge_initialise, shared_from_this(), purge_item),
						boost::bind(&runner::move_forward, shared_from_this(), purge_item, _1),
						state_reactor<>::very_low, false);
			}
		}
	}

	/**
	 * enqueue purge
	 *
	 * @return new max id
	 */
	uint64_t batch_purge(const archiving::org_id_t& org_id, const uint64_t& max_retention_id, const unsigned int batch_count)
	{
		static const boost::function< bool(const archiving::archivable::identifier&) > fn_is_filetype_auto_expired(
				boost::bind(&app::retention_purge::runner::is_auto_expiring, shared_from_this(), _1));

		int batch_count_(batch_count);

		// look for any on-demand purge
		{
			const retention_record::set_t& purgeables(
					retention_db_.get_retention_for_purge_ondemand(api_, org_id,
							boost::bind(retention_record::is_purgeable, boost::ref(cached_policy_finder_), _1, false),
							fn_is_filetype_auto_expired));

			if(!purgeables.empty()) {
				batch_count_ -= purgeables.size();

				schedule_purgeables(purgeables);
			}
		}

		// continue with the regular purge
		if(batch_count_ > 0) {
			const std::pair< uint64_t, retention_record::set_t >& purgeables(
					retention_db_.get_retention_for_purge(
							api_, org_id, max_retention_id, batch_count, get_retention_purge_restrictions(),
							boost::bind(retention_record::is_purgeable, boost::ref(cached_policy_finder_), _1, true),
							fn_is_filetype_auto_expired));

			if(!purgeables.second.empty())
				schedule_purgeables(purgeables.second);

			return purgeables.first;
		}

		return max_retention_id;
	}

	bool purge_initialise(item::pointer p_item) try
	{
		storage::s3::backend::pointer storage_backend = storage::s3::backend::get_instance();

		storage::string_map_t props;
		try {
			boost::static_pointer_cast<storage::s3::backend>(storage_backend)->stat(
					p_item->endpoint, p_item->identifier, props);
		}
		catch(const std::exception& e) {
			bool const* isKeyNotFound = boost::get_error_info< storage::s3::KeyNotFound_error_info >(e);
	        if (isKeyNotFound && *isKeyNotFound) {
	        	LogMessage("HEAD "<<p_item->identifier<<" failed; err:"<<e.what());

	        	if(is_auto_expiring(p_item->identifier)) {
	        		LogMessage("HEAD "<<p_item->identifier<<" failed; Assuming auto-expiry on storage endpoint "<<p_item->endpoint->id);
	        		return true;
	        	}

        		LogError("HEAD on "<<p_item->identifier<<" failed; could be data loss in storage endpoint "<<p_item->endpoint->id);
        		return false;
	        }

	        if (std::string const* statusStr = boost::get_error_info< storage::s3::StatusStr_error_info >(e))
	        	LogError("HEAD "<<p_item->identifier<<" failed; err:"<<e.what()<<" Status:"<<*statusStr);
	        else
	        	LogError("HEAD "<<p_item->identifier<<" failed; err:"<<e.what());

	        return false;
		}

		LogDebug("stat("<<p_item->identifier<<") "<<props);

		if(!props.size())
			THROW_RML_EXCEPTION("could not stat item on endpoint:"<<p_item->endpoint->id);

		if(props.find("clear-size") != props.end())
			p_item->clear_size = rml::utils::strtoull(props["clear-size"].c_str(), 0ULL);

		if(props.find("content-length") != props.end())
			p_item->real_size = rml::utils::strtoull(props["content-length"].c_str(), 0ULL);

#define AssertProps(key) \
		if(props.find(key) == props.end()) throw rml::exception(key" not found in HEAD response")

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
				rml::utils::strtoll(props["policy-id"].c_str(), 0LL),
				rml::utils::strtoull(props["policy-version"].c_str(), 0ULL),
				boost::gregorian::from_simple_string(props["creation-date"]).year_month_day(),
				boost::gregorian::from_simple_string(props["retention-min-date"]).year_month_day(),
				boost::gregorian::from_simple_string(props["retention-max-date"]).year_month_day(),
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
	bool reload_from_database() try
	{
		static const bool require_approval(confMap_["retention_purge.require_approval"].as<bool>());
		static const uint64_t max_id(internal_db_.get_purge_queue_max_id());

		static uint64_t loaded_max_id(0);

		if (!require_approval && !(loaded_max_id < max_id)) {
			LogMessage("Approval not required and we have finished reloading");
			return false;
		}

		std::string where(
				boost::str(
						boost::format("`HostName`='%1%' AND (`OrgID` %% @@auto_increment_increment) = (@@auto_increment_offset %% @@auto_increment_increment)")
						% app::hostname));

		if(loaded_max_id < max_id)
			where += boost::str(
					boost::format(" AND %7% ( ID < %1% AND ID > %2% "
							"AND State NOT IN ( "
							" %3% /* init_error */, %4% /* approve */, %6% /* approve_error */ "
							") ) ")
					% max_id % loaded_max_id
					% state::init_error % state::approve % state::approve_error
					% (require_approval? '(': ' '));

		if(require_approval)
			where += boost::str(
					boost::format(" %1% `State`=%2% /* approve_ok */ %3%")
					% ((loaded_max_id < max_id)? " OR ": " AND ") % state::approve_ok % ((loaded_max_id < max_id)? ')': ' '));

		const int& limit(get_limit());
		if(limit <= 0)
			return require_approval || (loaded_max_id >= max_id);

		item::list_t loaded_items;

		/**
		 * Select all those that need reloading after a restart OR the ones that need approval.
		 * Here once we load them, all approved ones are set to state::delete_file, just to mark as loaded.
		 */
		internal_db_.batch_load< item, item::extract_id >(
				loaded_items,
				boost::bind(item::setup_select, _1, _2, where),                             /* fn_setup_select */
				boost::bind(item::make_from_row, _1, boost::ref(storage_endpoint_finder_)), /* fn_make_item_from_row */
				boost::bind(item::setup_update_by_ids_begin, _1, _2,                        /* fn_setup_update_by_ids_begin */
						boost::str(boost::format("`State`=%1% /* delete_file */") % state::delete_file)),
				boost::bind(item::setup_update_by_ids_end, _1, _2,                          /* fn_setup_update_by_ids_end */
						boost::str(boost::format(" AND `State`=%1% /* approve_ok */") % state::approve_ok)),
				limit                                                                       /* select_limit */
		);

		// if none found, we need to continue only if require_approval == true
		if(loaded_items.empty())
			return require_approval || (loaded_max_id >= max_id);

		for(item::list_t::const_iterator itr = loaded_items.begin(); itr != loaded_items.end(); ++itr) {
			const uint64_t& id((*itr)->id);

			if(loaded_max_id < id)
				loaded_max_id = id;

			if((*itr)->cur_state == state::init)
				// initialise and move forward
				reactor_.schedule(
						boost::bind(&runner::purge_initialise, shared_from_this(), *itr),
						boost::bind(&runner::move_forward, shared_from_this(), *itr, _1),
						state_reactor<>::very_low, false);
			else
				move_forward(*itr);
		}

		return require_approval || (loaded_max_id >= max_id);
	}
	catch(const std::exception& e) {
		LogWarning("Exception: "<<e.what());
		return false;
	}
	catch(...) {
		LogWarning("Exception: Unknown or Unexpected");
		return false;
	}

	inline bool is_auto_expiring(const archiving::archivable::identifier& i) const
	{
		return auto_expire_filetype_[(int)(i.filetype)];
	}

	inline void move_forward(item::pointer p_item)
	{
		const state::outcome_t& cur_outcome(state::get_outcome(p_item->cur_state));
		switch(cur_outcome) {
		case state::ok:
			move_forward(p_item, true);
			break;

		case state::unknown:
		case state::interrupted:
		case state::error:
			p_item->cur_state = state::make(p_item->cur_state, state::unknown);
			perform_current_state_action(p_item);
			break;
		}
	}

	inline void move_forward(item::pointer p_item, const bool& outcome_ok) try
	{
		static const unsigned int db_flush_interval_seconds(
				confMap_["retention_purge.db_flush_interval_seconds"].as<unsigned int>());

		p_item->cur_state = state::make(p_item->cur_state, outcome_ok);

		// if not dummy item
		if(p_item->retention) {

			// set up save()
			internal_db_.batch_save< item >(p_item);

			// keep it available till the save() fires!
			__pc_produce(item_save_channel_, p_item, 15 + db_flush_interval_seconds, 0, 0);
		}

		try {
			// consume and discard if any save()d
			item::channel_t::bare_item_set_t saved_items;
			item_save_channel_.consume(saved_items, 0, 100, boost::bind(item::is_save_not_pending, _1));
			LogDebug(saved_items.size()<<" items found saved; discarded");
		}
		catch(const item::channel_t::consume_timed_out_exception& e) { /* ignored */ }

		if (outcome_ok) {
			if(p_item->cur_state != state::finished)
				p_item->cur_state = state::next_up(p_item->cur_state);
			perform_current_state_action(p_item);
		}
		else {
			// TODO handle errors mid way!!!
			// currently we will retry when archived restarts
			LogWarning(p_item<<" Failed");
		}
	}
	catch(const item::channel_t::finishing_exception& e) {
		// we are shutting down; free up the call back asap
		return;
	}
	catch(const std::exception& e) {
		LogWarning("Exception: "<<e.what()<<" [unexpected but ignored]");
		return;
	}
	catch(...) {
		LogWarning("Exception: unknown [unexpected but ignored]");
		return;
	}

	inline void perform_current_state_action(item::pointer p_item)
	{
		static const boost::function< bool(const archiving::archivable::identifier&) > fn_is_filetype_auto_expired(
				boost::bind(&app::retention_purge::runner::is_auto_expiring, shared_from_this(), _1));

		static const bool require_approval(confMap_["retention_purge.require_approval"].as<bool>());

		// --- ---

		// if we need no approval; skip this stage
		if(state::get_coarse(p_item->cur_state) == state::_approve_ && !require_approval)
			p_item->cur_state = state::next_up(p_item->cur_state, /* force = */true);

		switch(state::get_coarse(p_item->cur_state)) {

		case state::_approve_:
		{
			// nothing to do here as we have already saved it! Just that we wouldn't take it further in the
			// state machine; dead end ( for now )!
			break;
		}

		case state::_delete_file_:
		{
			if(p_item->retention && auto_expire_filetype_[(int)(p_item->identifier.filetype)]) {
				LogDebug(p_item<<" - skipping delete_file stage as it is auto-expired");

				// route it via the reactor so that we can distribute work over threads, rather than moving forward with this thread
				reactor_.schedule(
						boost::bind(&runner::purge_stage_done, shared_from_this(), p_item),
						boost::bind(&runner::move_forward, shared_from_this(), p_item, _1),
						state_reactor<>::low, false);
			}

			if(p_item->retention) // if not dummy
				__pc_produce(delete_file_channel_, p_item);

			if(((time(0) - last_delete_file_time_) < purge_file_interval_seconds) && (delete_file_channel_.count() < purge_file_batch_size))
				break;

			try {
				boost::mutex::scoped_lock lock(last_delete_file_time_mutex_, boost::try_to_lock);
				if(lock) {
					last_delete_file_time_ = time(0);

					item::endpoint_grouped_multiset_t purge_set;
					try {
						delete_file_channel_.consume(purge_set, 0, 500000);
					}
					catch(const item::channel_t::consume_timed_out_exception& e) {
					}
					catch(const item::channel_t::finishing_exception& e) {
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
			if(p_item->retention) // if not dummy
				__pc_produce(delete_usage_channel_, p_item);

			if(((time(0) - last_delete_usage_time_) < purge_usage_interval_seconds) && (delete_usage_channel_.count() < purge_usage_batch_size))
				break;

			try {
				boost::mutex::scoped_lock lock(last_delete_usage_time_mutex_, boost::try_to_lock);
				if(lock) {
					last_delete_usage_time_ = time(0);

					item::type_org_endpoint_grouped_multiset_t purge_set;
					try {
						delete_usage_channel_.consume(purge_set, 0, 500000);
					}
					catch(const item::channel_t::consume_timed_out_exception& e) {
					}
					catch(const item::channel_t::finishing_exception& e) {
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
									done_ok = retention_db_.usage_purge(
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
									__pc_produce(delete_usage_channel_, p_item, 600, 0, 0);
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
			if(p_item->retention) // if not dummy
				__pc_produce(delete_retention_channel_, p_item);

			if(((time(0) - last_delete_retention_time_) < purge_retention_interval_seconds) && (delete_retention_channel_.count() < purge_retention_batch_size))
				break;

			try {
				boost::mutex::scoped_lock lock(last_delete_retention_time_mutex_, boost::try_to_lock);
				if(lock) {
					last_delete_retention_time_ = time(0);

					item::org_grouped_multiset_t purge_set;
					try {
						delete_retention_channel_.consume(purge_set, 0, 500000);
					}
					catch(const item::channel_t::consume_timed_out_exception& e) {
					}
					catch(const item::channel_t::finishing_exception& e) {
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
										item::extract_retention_id, item::is_filetype_cdr >(
												(*itr_batch.first)->identifier.org,
												itr_batch.first, itr_batch.second,
												confMap_["retention_purge.delete_cdr_retention_records"].as<bool>());
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
									__pc_produce(delete_retention_channel_, p_item, 600, 0, 0);
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
			// set saved so that we wouldn't re-insert the item after we delete
			item::set_save_pending(p_item, false);

			if(p_item->retention) // if not dummy
				__pc_produce(finished_channel_, p_item);

			if(((time(0) - purge_finished_time_) < purge_finished_interval_seconds) && (finished_channel_.count() < purge_finished_batch_size))
				break;

			try {
				boost::mutex::scoped_lock lock(purge_finished_time_mutex_, boost::try_to_lock);
				if(lock) {
					purge_finished_time_ = time(0);

					item::list_t purge_list;
					try {
						finished_channel_.consume(purge_list, 0, 500);
					}
					catch(const item::channel_t::consume_timed_out_exception& e) {
						break;
					}
					catch(const item::channel_t::finishing_exception& e) {
						break;
					}

					if(!purge_list.empty()) {
						internal_db_.delete_by_ids< item, item::list_t::const_iterator, item::extract_id, item::extract_identifier >(
										purge_list.begin(), purge_list.end(), "ArchiveDPurgeQueue", "ID");

						// to adjust for delays from processing the batch
						purge_finished_time_ = time(0);
					}
				}
			}
			catch(const boost::lock_error& e) {
				LogVerbose("Failed to gain purge-finished-lock; skipping... [exception:"<<e.what()<<']');
			}

			break;
		}

		default:
			LogCritical(p_item<<" Unknown state; cannot move forward");
			break;
		}
	}

	enum
	{
		put_rate_sample_size = 2
	};

	inline int get_average_put_rate()
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

	inline int get_limit()
	{
		int average_put_rate_5mins = get_average_put_rate();
		if(average_put_rate_5mins < 0) {
			LogMessage("Not enough samples for purge rate-limiting; continuing to wait...");
			return -1;
		}

		if(average_put_rate_5mins >= put_rate_cut_off) {
			LogMessage("Purge rate-limiting: average PUT rate (5mins) found above cut-off rate [ "<<average_put_rate_5mins<<" >= "<<put_rate_cut_off<<" ]");
			return -1;
		}

		// compute the limit based on put rate from statistics
		return (average_put_rate_5mins > put_rate_zero)?
						( min_limit_object + (
								(max_limit_object - min_limit_object) * (
										(average_put_rate_5mins - put_rate_zero) / (put_rate_cut_off - put_rate_zero))) )
						: max_limit_object;
	}

	/**
	 * find records from Retention DB and pool them into Purge Queue
	 */
	void feed_purge_queue()
	{
		int iter = 1;

		types::purge_def::pointer p_purge_def;

		while(!quit_) {
			if(!quit_ && (++iter % 10)) {
				boost::this_thread::sleep(boost::posix_time::seconds(1));
				continue;
			}

#define LogChannelCount(channel) \
			LogMessage("RetentionPurge::"<<channel<<" Count: "<<channel.count())

			LogChannelCount(item_save_channel_);
			LogChannelCount(delete_file_channel_);
			LogChannelCount(delete_usage_channel_);
			LogChannelCount(delete_retention_channel_);
			LogChannelCount(finished_channel_);

#undef LogChannelCount

			const int iter_tens((iter / 10) % 10);
			if(!quit_ && (iter_tens >= 0 && iter_tens <= 4) ) {

				// here we make the batching mechanism to fire-off the state; they could be starving for more items
				// so we fire-off dummy items into the channel(s) to tickle the various states

				LogDebug("Dummy item ... "<<iter_tens);

				switch(iter_tens) {
				case 0:
				{
					// TODO if we ever remove the approve stage, we need to do mods here

					// tinker the delete_file state
					item::pointer purge_item(new item(state::approve));
					reactor_.schedule(
							boost::bind(&runner::purge_stage_done, shared_from_this(), purge_item),
							boost::bind(&runner::move_forward, shared_from_this(), purge_item, _1),
							state_reactor<>::high, false);
					break;
				}
				case 1:
				{
					// tinker the delete_usage state
					item::pointer purge_item(new item(state::delete_file));
					reactor_.schedule(
							boost::bind(&runner::purge_stage_done, shared_from_this(), purge_item),
							boost::bind(&runner::move_forward, shared_from_this(), purge_item, _1),
							state_reactor<>::high, false);
					break;
				}
				case 2:
				{
					// tinker the delete_retention state
					item::pointer purge_item(new item(state::delete_usage));
					reactor_.schedule(
							boost::bind(&runner::purge_stage_done, shared_from_this(), purge_item),
							boost::bind(&runner::move_forward, shared_from_this(), purge_item, _1),
							state_reactor<>::high, false);
					break;
				}
				case 3:
				{
					// tinker the finished state
					item::pointer purge_item(new item(state::delete_retention));
					reactor_.schedule(
							boost::bind(&runner::purge_stage_done, shared_from_this(), purge_item),
							boost::bind(&runner::move_forward, shared_from_this(), purge_item, _1),
							state_reactor<>::high, false);
					break;
				}
				case 4:
				{
					// tinker the finished state
					item::pointer purge_item(new item(state::finished));
					reactor_.schedule(
							boost::bind(&runner::purge_stage_done, shared_from_this(), purge_item),
							boost::bind(&runner::move_forward, shared_from_this(), purge_item, _1),
							state_reactor<>::high, false);
					break;
				}

				default: // we shouldnt get here!
				{
					LogWarning("iter_test: "<<iter_tens<<" - Unexpected");
					break;
				}
				}
			}

			if(!quit_ && (iter % 60))
				continue;

			if(quit_)
				break;

			try {

				{
					const long& purge_queue_size(internal_db_.get_purge_queue_size(purge_max_queue_size));
					if(purge_queue_size < 0) {
						LogWarning("Failed to obtain purge queue size");
						continue;
					}

					// check if we need to feed the queue
					if (purge_max_queue_size < purge_queue_size) {
						LogMessage("Purge queue size >= "<<purge_max_queue_size<<"; not en-queueing anymore");
						continue;
					}
				}

				const int& limit(get_limit());
				if(limit < 0)
					continue;

				if(!p_purge_def) {
					LogDebug("First loading of purge def!");
					p_purge_def = retention_db_.get_orgs_for_purge();
				}

				p_purge_def = internal_db_.update_get_purge_def(p_purge_def, limit, confMap_["retention_purge.enabled_orgs"].as<std::string>());
				if(!p_purge_def) {
					LogVerbose("Failed to update and get purge definition; skipping attempt");
					continue;
				}

				if (!p_purge_def->set.empty()) {
					types::purge_def::set_t orgs_update;

					const types::purge_def::set_t::const_iterator& end(p_purge_def->set.end());
					for(types::purge_def::set_t::const_iterator itr = p_purge_def->set.begin(); !quit_ && (itr != end); ++itr) {
						const long& purge_queue_size(internal_db_.get_purge_queue_size(purge_max_queue_size));
						if(purge_queue_size < 0) {
							LogWarning("Failed to obtain purge queue size");
							continue;
						}

						// make sure we dont over fill
						if (purge_max_queue_size > purge_queue_size) {
							// readjust limit based on purge queue size
							const int limit_now(std::min< int >(limit, purge_max_queue_size - purge_queue_size));

							uint64_t new_max_id(batch_purge(itr->org_id, itr->max_retention_id, limit_now));
							if(new_max_id == itr->max_retention_id) {
								LogMessage("Org:"<<itr->org_id<<" no purgables found"<<(itr->max_retention_id? "; resetting to zero": ""));
								new_max_id = 0;
							}

							orgs_update.insert(types::purge_def::org_def(itr->org_id, new_max_id));
						}
					}

					p_purge_def->set.swap(orgs_update);
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

#endif /* RETENTION_PURGE_RUNNER_HPP_ */
