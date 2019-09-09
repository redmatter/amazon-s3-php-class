/*
 * retention_data_flusher.hpp
 *
 *  Created on: 26 May 2011
 *      Author: dkorah
 */

#ifndef RETENTION_DATA_FLUSHER_HPP_
#define RETENTION_DATA_FLUSHER_HPP_

#include "database_retention.hpp"

#include <boost/chrono/duration.hpp>
#include <boost/chrono/io/time_point_io.hpp>
#include <boost/thread/thread.hpp>
#include <boost/container/slist.hpp>

namespace app {

class retention_data_flusher
{
	operation_data::channel_t& retention_data_channel_;
	operation_data::channel_t& purge_channel_;
	database::retention& retention_db_;
	database::internal& internal_db_;

	volatile bool& quit_;

	const boost::chrono::seconds flush_interval_seconds_;
	const boost::chrono::seconds min_flush_interval_seconds_;
	const unsigned int flush_max_batch_size_;

public:
	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("retention_data.flush_max_batch_size", boost::program_options::value<unsigned int>()->default_value(4000U), "retention data flush - maximum batch size")
				("retention_data.flush_interval_seconds", boost::program_options::value<unsigned int>()->default_value(10U), "retention data flush - interval in seconds")
				("retention_data.min_flush_interval_seconds", boost::program_options::value<unsigned int>()->default_value(3U), "retention data flush - minimum interval between attempts, in seconds")
				;
	}

	retention_data_flusher(const boost::program_options::variables_map &confMap,
						   operation_data::channel_t &retention_data_channel, operation_data::channel_t &purge_channel,
						   database::retention &retention_db, database::internal &internal_db, volatile bool &quit) :
		retention_data_channel_(retention_data_channel),
		purge_channel_(purge_channel),
		retention_db_(retention_db),
		internal_db_(internal_db),
		quit_(quit),
		flush_interval_seconds_(confMap["retention_data.flush_interval_seconds"].as<unsigned int>()),
		min_flush_interval_seconds_(
				std::min(
						static_cast<unsigned int>(flush_interval_seconds_.count()),
						std::max(1U, confMap["retention_data.min_flush_interval_seconds"].as<unsigned int>()))),
		flush_max_batch_size_(std::max(500U, confMap["retention_data.flush_max_batch_size"].as<unsigned int>()))
	{
	}

	void run() try {
		// delay the first attempt by 20 seconds
		boost::chrono::steady_clock::time_point next_attempt =
				boost::chrono::steady_clock::now() + boost::chrono::seconds(20);
		while (!quit_) {
			boost::this_thread::sleep_until(next_attempt);
			const boost::chrono::steady_clock::time_point& attempt_start_time(boost::chrono::steady_clock::now());

			database::retention::data::multiset_t retention_set;
			database::retention::usage_data::multiset_t usage_data_set;

			boost::container::slist<operation_data::pointer> saved_items;

			try {
				bool channel_finishing = false;

				// disable interruption via boost::thread::terminate()
				// otherwise it can affect consume() from pc_channel as it uses conditional-wait which is one of the
				// interruption points.
				boost::this_thread::disable_interruption disable_interruption;

				unsigned int batch_max = flush_max_batch_size_;
				while (!quit_ && batch_max--) {
					try {
						operation_data::pointer data = retention_data_channel_.consume(0, 1000);
						const archiving::archivable::identifier& identifier(data->p_detail->identifier);

						time_t creation_time;
						{
							const types::metadata::pointer& p_metadata( data->p_detail->p_metadata );
							types::metadata::const_iterator itr = p_metadata->find("CreateTime");
							if (itr == p_metadata->end()) {
								LogWarning("flush-retention: " << identifier<<" CreateTime NOT found in metadata");
								time(&creation_time);
							} else {
								creation_time = rml::utils::strtoull(itr->second.c_str());
							}
						}

						if (data->p_detail->state == operation_state::put_file_save_ok) {
							LogDebug("flush-retention: " << identifier<<": ToRetention");
							{
								database::retention::data retention;
								retention.file_format = data->p_detail->target_file_format;
								retention.file_type = identifier.filetype;
								retention.org = identifier.org;
								retention.uuid = identifier.uuid;
								retention.retention = app::applied_policy(data->p_detail->policy.p_policy, creation_time);
								retention.endpoint_id = data->p_detail->p_endpoint->id;
								retention.user = 0;
								{
									// TODO solve multi user situation
									const types::metadata::pointer& p_metadata( data->p_detail->p_metadata );
									types::metadata::const_iterator itr = p_metadata->find("UserIDs");
									if(itr != p_metadata->end()) {
										std::string user(itr->second, 0, itr->second.find(','));
										retention.user = rml::utils::strtoll(user.c_str());
									}
								}

								{
									const types::metadata::pointer& p_metadata( data->p_detail->p_metadata );
									types::metadata::const_iterator itr = p_metadata->find("LegUUID");
									if (itr != p_metadata->end()) {
										retention.leg_uuid = itr->second;
									}
								}

								retention_set.insert(retention);
							}

							{
								database::retention::usage_data usage;
								usage.org = identifier.org;
								usage.filetype = identifier.filetype;
								usage.create_time = creation_time;
								usage.clear_size = data->p_detail->clear_size;
								usage.real_size = data->p_detail->real_size;
								usage.endpoint_id = data->p_detail->p_endpoint->id;

								usage_data_set.insert(usage);
							}

							data->p_detail->state = operation_state::all_done;
						} else {
							LogMessage("flush-retention: " << identifier<<": Not archived (no retention record created)");
						}

						// collect all items for release to purge-channel later on
						saved_items.push_front(data);
					} catch (const operation_data::channel_t::finishing_exception& e) {
						channel_finishing = true;
						break;
					} catch (const operation_data::channel_t::consume_timed_out_exception& e) {
						break;
					}
				}

				if (!retention_set.empty() || !usage_data_set.empty()) {
					typedef boost::chrono::duration<double, boost::ratio<60> > double_minute;
					const boost::chrono::steady_clock::time_point when_started = boost::chrono::steady_clock::now();
					int retry = 0;
					while(!retention_db_.save(retention_set, usage_data_set)) {
						const boost::chrono::seconds retry_interval(10);
						LogWarning("flush-retention: [retry: " << (++retry) << "] Retention save failed; retrying in "
											  << retry_interval.count() << "s (DO NOT KILL THE PROCESS) ... [running for "
											  << boost::chrono::duration_cast<double_minute>(
													  boost::chrono::steady_clock::now() - when_started).count()
											  << " minutes]");
						boost::this_thread::sleep_for(retry_interval);
					}
				}

				// if the channel is finishing or quit flagged, we need to save the latest state and break from the loop.
				// otherwise the items are moved on to the purge channel.
				if (channel_finishing || quit_) {
					boost::container::slist<operation_data::pointer>::iterator end = saved_items.end();
					boost::container::slist<operation_data::pointer>::iterator itr = saved_items.begin();
					for (; itr != end; ++itr) {
						internal_db_.save(*itr);
					}

					break;
				}

				boost::container::slist<operation_data::pointer>::iterator end = saved_items.end();
				boost::container::slist<operation_data::pointer>::iterator itr = saved_items.begin();
				for (; itr != end; ++itr) {
					pc_produce(purge_channel_, *itr);
				}
			} catch (const operation_data::channel_t::finishing_exception& e) {
				break;
			} catch (const std::exception& e) {
				LogError("flush-retention: Exception: "<<e.what());
			} catch (...) {
				LogError("flush-retention: Unexpected exception");
			}

			const boost::chrono::steady_clock::time_point& attempt_end_time(boost::chrono::steady_clock::now());
			LogMessage("flush-retention: attempt took " << boost::chrono::duration_cast<boost::chrono::duration<double> >(attempt_end_time - attempt_start_time).count() << "s");

			// next attempt based on attempt start time so that there is more chance of reducing a built-up backlog due
			// to spike in traffic.
			next_attempt = attempt_start_time + flush_interval_seconds_;
			if (next_attempt < (attempt_end_time + min_flush_interval_seconds_)) {
				next_attempt = attempt_end_time + min_flush_interval_seconds_;
			}

			LogDebug("flush-retention: Next attempt @ " << next_attempt);
		}

		LogMessage("flush-retention: terminating");
	} catch (const boost::thread_interrupted &e) {
		LogMessage("flush-retention: thread interrupted");
	} catch (const std::exception &e) {
		LogMessage("flush-retention: thread ending due to exception: " << e.what());
	} catch (...) {
		LogMessage("flush-retention: thread ending due to unknown exception");
	}
};

}  // namespace app

#endif /* RETENTION_DATA_FLUSHER_HPP_ */
