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

#include <boost/interprocess/sync/interprocess_mutex.hpp>

namespace app {

struct monitor_item
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

	monitor_item() :
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
		store_retention_count(0)
	{
	}

};

struct monitor
{
	size_t n_items;
	monitor_item items[60];
	
	boost::interprocess::interprocess_mutex mutex;
	
	monitor() : n_items(0)
	{
	}
	
	void add_item(const monitor_item& item)
	{
		if(n_items) {
			for(size_t i = std::min<size_t>(n_items + 1, 60); i > 1; --i)
				memcpy(&items[i - 1], &items[i - 2], sizeof(monitor_item));
		}
		
		memcpy(&items[0], &item, sizeof(monitor_item));
		n_items = std::min<size_t>(n_items + 1, 60);
	}
};

}  // namespace app


#endif /* MONITOR_HPP_ */
