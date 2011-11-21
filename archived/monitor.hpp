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

#include <string.h>

#include <iostream>

#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/thread/mutex.hpp>

namespace app {

namespace monitor {


struct monitor_t
{
	struct counter_t
	{
		size_t put;
		size_t cached_only;
		size_t stored;
		size_t stored_size;
		
		counter_t() : put(0), cached_only(0), stored(0), stored_size(0)
		{
		}
	};
	
	struct item_t
	{
		time_t sample_time;

		size_t convert_waiting;
		size_t convert_done_waiting;
		size_t storage_ops_waiting;
		size_t retention_store_waiting;
		size_t purge_waiting;
		size_t whisper_waiting;

		bool is_db_loading;

		size_t store_put_count;
		size_t store_put_size;
		size_t store_convert_count;
		size_t store_save_count;
		size_t store_retention_count;
		
		counter_t counter;

		item_t() :
			sample_time(0),

			convert_waiting(0),
			convert_done_waiting(0),
			storage_ops_waiting(0),
			retention_store_waiting(0),
			purge_waiting(0),
			whisper_waiting(0),

			is_db_loading(false),

			store_put_count(0),
			store_put_size(0),
			store_convert_count(0),
			store_save_count(0),
			store_retention_count(0),

			counter()
		{
		}
		
		friend inline std::ostream& operator << (std::ostream& ostr, const item_t& item)
		{
			return ostr << item.sample_time<<" Convert:"<<item.convert_waiting<<" ConvertDone:"<<item.convert_done_waiting<<" StorageOps:"<<item.storage_ops_waiting<<" RetentionData:"<<item.retention_store_waiting<<" Purge:"<<item.purge_waiting<<" Whisper:"<<item.whisper_waiting<<" DBLoading:"<<(item.is_db_loading? "yes": "no")<<" counters:"<<item.counter.put<<'/'<<item.counter.cached_only<<'/'<<item.counter.stored<<' '<<item.counter.stored_size;
		}
	};

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
		stored_size
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
	}
}

inline monitor_t::counter_t get_counter()
{
	boost::unique_lock<boost::mutex> lock(counter_mutex);
	return counter;
}

}  // namespace monitor

}  // namespace app


#endif /* MONITOR_HPP_ */
