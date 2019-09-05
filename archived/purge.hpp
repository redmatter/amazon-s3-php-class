/*
 * purge.hpp
 *
 *  Created on: 10 Oct 2011
 *      Author: dkorah
 */

#ifndef PURGE_HPP_
#define PURGE_HPP_

#include "operation_data.hpp"

namespace app {

class purge
{
	operation_data::channel_t& purge_channel_;
	database::internal& internal_db_;
	operation_data::busy_set_t& operation_data_busy_set_;
	const volatile bool& quit_;

	const boost::program_options::variables_map& confMap_;

	enum constants
	{
		min_interval_seconds = 30
	};

public:
	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("purge.flush_max_batch_size", boost::program_options::value<unsigned int>()->default_value(4000U), "purge - maximum batch size")
				("purge.interval_seconds",  boost::program_options::value<unsigned int>()->default_value(min_interval_seconds), "purge - interval in seconds")
				("purge.min_interval_seconds", boost::program_options::value<unsigned int>()->default_value(3U), "purge - minimum interval between attempts, in seconds")
				;
	}

	purge(operation_data::channel_t& purge_channel, database::internal& internal_db, operation_data::busy_set_t& operation_data_busy_set,
			const volatile bool& quit, const boost::program_options::variables_map& confMap) :
		purge_channel_(purge_channel), internal_db_(internal_db), operation_data_busy_set_(operation_data_busy_set), quit_(quit), confMap_(confMap)
	{
	}

	void run()
	{
		const boost::chrono::seconds flush_interval_seconds_(confMap["purge.interval_seconds"].as<unsigned int>());
		const boost::chrono::seconds min_flush_interval_seconds_(
				std::min(
						static_cast<unsigned int>(flush_interval_seconds_.count()),
						std::max(1U, confMap["purge.min_interval_seconds"].as<unsigned int>())));
		const unsigned int flush_max_batch_size_(std::max(500U, confMap["purge.flush_max_batch_size"].as<unsigned int>()));

		// delay the first attempt by 20 seconds
		boost::chrono::steady_clock::time_point next_flush =
				boost::chrono::steady_clock::now() + boost::chrono::seconds(20);
		while (!quit_) {
			boost::this_thread::sleep_until(next_flush);
			const boost::chrono::steady_clock::time_point& attempt_start_time(boost::chrono::steady_clock::now());

			// disable interruption via boost::thread::terminate()
			// otherwise it can affect consume() from pc_channel as it uses conditional-wait which is one of the
			// interruption points.
			boost::this_thread::disable_interruption disable_interruption;

			unsigned int batch_size = flush_max_batch_size_;
			boost::container::slist<operation_data::pointer> purge_items;

			bool channel_finishing = false;

			// loop and collect till we have a batch or there is nothing in the channel or it is being closed
			while (!quit_ && batch_size-- > 0) {
				try {
					operation_data::pointer opdata = purge_channel_.consume(1);

					files_map_t::const_iterator itr = opdata->p_detail->files_map.begin();
					for (; itr != opdata->p_detail->files_map.end(); ++itr) {
						itr->second->set_persistent(false);
					}

					purge_items.push_front(opdata);
					operation_data_busy_set_.remove(opdata); // RIP
				} catch(const operation_data::channel_t::finishing_exception& e) {
					channel_finishing = true;
					break;
				} catch (const operation_data::channel_t::consume_timed_out_exception& e) {
					break;
				}
				catch(const std::exception& e) {
					LogWarning("queue-purge: exception:" << e.what());
				}
			}

			if (!purge_items.empty()) {
				if (!internal_db_.purge(purge_items.begin(), purge_items.end())) {
					LogWarning("queue-purge: failed to purge " << purge_items.size() << "items");
				}
			}

			if (channel_finishing) {
				break;
			}

			const boost::chrono::steady_clock::time_point& attempt_end_time(boost::chrono::steady_clock::now());
			LogMessage("queue-purge: attempt took " << boost::chrono::duration_cast<boost::chrono::duration<double> >(attempt_end_time - attempt_start_time).count() << "s");

			// next attempt based on attempt start time so that there is more chance of reducing a built-up backlog due
			// to spike in traffic.
			next_flush = attempt_start_time + flush_interval_seconds_;
			if (next_flush < (attempt_end_time + min_flush_interval_seconds_)) {
				next_flush = attempt_end_time + min_flush_interval_seconds_;
			}

			LogDebug("queue-purge: Next attempt @ " << next_flush);
		}
	}
};

}  // namespace app


#endif /* PURGE_HPP_ */
