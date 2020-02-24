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
#include "../common/archived/stats/trend.hpp"

#include <boost/atomic.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/optional.hpp>

#include <iostream>

namespace {
inline common::archived::stats::trend&
trend_()
{
	static common::archived::stats::trend trend;
	return trend;
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

	inline static std::size_t max_samples() { return sizeof(monitor_t::items) / sizeof(monitor_t::item_t); }

	void add_item(const item_t& item)
	{
		if (n_items) {
			for (size_t i = std::min<size_t>(n_items + 1, 60); i > 1; --i)
				memcpy(&items[i - 1], &items[i - 2], sizeof(item_t));
		}

		memcpy(&items[0], &item, sizeof(item_t));
		n_items = std::min<size_t>(n_items + 1, max_samples());
	}
};

inline common::archived::stats::trend const&
capture_trend(const monitor_t* m, const boost::chrono::minutes& mins)
{
	return trend_() = common::archived::stats::trend::make_from_samples(mins, m->n_items, m->items);
}

inline common::archived::stats::trend const&
get_trend()
{
	return trend_();
}

inline boost::optional<std::time_t>
should_neglect_pcaps(std::size_t watermark, unsigned int neglect_duration_minutes)
{
	using common::archived::stats::trend;

	static boost::atomic<std::time_t> neglect_until(0);

	// till we have a trend, don't neglect PCAPs
	const trend& t = get_trend();
	if (!t.is_valid()) {
		return boost::none;
	}

	// once we start neglecting, continue neglecting for set duration
	std::time_t neglect_until_ = neglect_until.load(boost::memory_order_relaxed);
	if (neglect_until_ != 0 && t.time < neglect_until_) {
		return neglect_until_;
	}

	// see if we can stop neglecting (or start to, if we haven't been already)
	bool neglect = true;
	if (t.get_backlog() < watermark) { // if the backlog is below watermark
		neglect = false;
	} else if (t.value == trend::down &&
			   t.slope < -15) { // though above watermark, if the backlog has been  consistently coming down for the
								// past X minutes, at a considerable rate
		neglect = false;
	}

	neglect_until_ = 0;
	if (neglect) {
		neglect_until_ = std::time(0) + neglect_duration_minutes * 60;
	}

	neglect_until = neglect_until_;
	if (!neglect_until_) {
		return boost::none;
	}

	return neglect_until_;
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
