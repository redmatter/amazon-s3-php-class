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

#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/thread/mutex.hpp>

#include <iostream>

namespace app {
namespace monitor {

struct monitor_t
{
    typedef common::archived::stats::item_t item_t;
    typedef common::archived::stats::counter_t counter_t;

	size_t n_items;
	item_t items[60];
	
	boost::interprocess::interprocess_mutex mutex;
	
	monitor_t() : n_items(0)
	{
	}
	
	void add_item(const item_t& item)
	{
		if(n_items) {
			for(size_t i = std::min<size_t>(n_items + 1, 60); i > 1; --i)
				memcpy(&items[i - 1], &items[i - 2], sizeof(item_t));
		}
		
		memcpy(&items[0], &item, sizeof(item_t));
		n_items = std::min<size_t>(n_items + 1, 60);
	}
};

}  // namespace monitor

extern monitor::monitor_t::counter_t counter;
extern boost::mutex counter_mutex;

namespace monitor {

struct counter_accessor
{
	enum counter_accessor_t
	{
		put,
		cached_only,
		stored,
		stored_size,
		call_buffering
	};
};

inline void increment_counter(const counter_accessor::counter_accessor_t& accessor, const size_t step = 1)
{
	boost::unique_lock<boost::mutex> lock(counter_mutex);

	switch(accessor)
	{
	case counter_accessor::put:
		counter.put += step;
		break;
		
	case counter_accessor::cached_only:
		counter.cached_only += step;
		break;
		
	case counter_accessor::stored:
		counter.stored += step;
		break;
		
	case counter_accessor::stored_size:
		counter.stored_size += step;
		break;

	case counter_accessor::call_buffering:
		counter.call_buffering += step;
		break;
	}
}

inline monitor_t::counter_t get_counter(const bool reset = false)
{
	boost::unique_lock<boost::mutex> lock(counter_mutex);
	if (reset) {
		monitor_t::counter_t ret = counter;
		counter = monitor_t::counter_t();
		
		return ret;
	}
	
	return counter;
}

}  // namespace monitor
}  // namespace app


#endif /* MONITOR_HPP_ */
