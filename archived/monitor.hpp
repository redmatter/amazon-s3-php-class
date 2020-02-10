/*
 * monitor.hpp
 *
 *  Created on: 19 Nov 2011
 *      Author: dkorah
 */

#ifndef MONITOR_HPP_
#define MONITOR_HPP_

#ifndef SHM_NAME
#define SHM_NAME "SHMArchiveDMonitor"
#endif

#include "../common/archived/stats/item.hpp"
#include "../common/archived/stats/snapshot.hpp"

#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/make_shared.hpp>

#include <iostream>

namespace {
boost::shared_ptr<common::archived::stats::snapshot>& get_snapshot_ptr()
{
	static boost::shared_ptr<common::archived::stats::snapshot> snapshot;
	return snapshot;
}
} // namespace

namespace app {
namespace monitor {

struct monitor_t
{
	typedef common::archived::stats::item_t item_t;
	typedef common::archived::stats::counter_t counter_t;

	size_t n_items;
	item_t items[60];

	boost::interprocess::interprocess_mutex mutex;

	monitor_t() : n_items(0) {}

	void add_item(const item_t& item)
	{
		if (n_items) {
			for (size_t i = std::min<size_t>(n_items + 1, 60); i > 1; --i)
				memcpy(&items[i - 1], &items[i - 2], sizeof(item_t));
		}

		memcpy(&items[0], &item, sizeof(item_t));
		n_items = std::min<size_t>(n_items + 1, 60);
	}
};

inline void make_trend_snapshot(const monitor_t* m, const boost::chrono::minutes& mins)
{
	get_snapshot_ptr() = boost::make_shared<common::archived::stats::snapshot>(mins, m->n_items, m->items);
}

inline common::archived::stats::trend get_trend()
{
	return get_snapshot_ptr()->get_trend();
}

inline bool should_neglect_pcaps(std::size_t watermark)
{
	using common::archived::stats::trend;

	const trend& t = get_trend();
	std::size_t combined_queue_levels =
	  t.convert_waiting + t.convert_done_waiting + t.storage_ops_waiting + t.retention_store_waiting;

	return combined_queue_levels > watermark && t.value != trend::down;
}

} // namespace monitor

extern monitor::monitor_t::counter_t counter;
extern boost::mutex counter_mutex;

namespace monitor {

inline void
increment_put(std::size_t step = 1)
{
	boost::unique_lock<boost::mutex> lock(counter_mutex);
	counter.increment_put(step);
}

inline void
increment_cached_only(std::size_t step = 1)
{
	boost::unique_lock<boost::mutex> lock(counter_mutex);
	counter.increment_cached_only(step);
}

inline void
increment_stored(std::size_t step = 1)
{
	boost::unique_lock<boost::mutex> lock(counter_mutex);
	counter.increment_stored(step);
}

inline void
increment_stored_size(std::size_t step = 1)
{
	boost::unique_lock<boost::mutex> lock(counter_mutex);
	counter.increment_stored_size(step);
}

inline void
increment_call_buffering(std::size_t step = 1)
{
	boost::unique_lock<boost::mutex> lock(counter_mutex);
	counter.increment_call_buffering(step);
}

inline monitor_t::counter_t
get_counter(const bool reset = false)
{
	boost::unique_lock<boost::mutex> lock(counter_mutex);
	if (reset) {
		monitor_t::counter_t ret = counter;
		counter = monitor_t::counter_t();

		return ret;
	}

	return counter;
}

} // namespace monitor
} // namespace app

#endif /* MONITOR_HPP_ */
