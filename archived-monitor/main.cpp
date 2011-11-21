/*
 * monitor.cpp
 *
 *  Created on: 19 Nov 2011
 *      Author: dkorah
 */

#include "../archived/monitor.hpp"

#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/date_time.hpp>

#include <iostream>

class exception : public std::exception {
private:
	const std::string m_err;
public:
	exception (const std::string &err) : m_err(err) {}
	virtual const char* what() const throw() { return &m_err[0]; }
	~exception() throw () {}
};

int main(int argc, char* argv[])
{
	try
	{
		boost::interprocess::shared_memory_object shm(boost::interprocess::open_only, SHM_NAME, boost::interprocess::read_write);

		boost::interprocess::mapped_region region(shm, boost::interprocess::read_write);
		app::monitor* monitor_data = static_cast<app::monitor*>(region.get_address());
		
		boost::posix_time::ptime till = boost::posix_time::microsec_clock::local_time() + boost::posix_time::milliseconds(500);
		boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> lock(monitor_data->mutex, till);
		if(lock) {
			if(!monitor_data->n_items)
				throw exception("No items");
			
			for(size_t i = 0; i < monitor_data->n_items; ++i) {
				const app::monitor_item& item(monitor_data->items[i]); 
				std::cout << item.sample_time<<" Convert:"<<item.convert_waiting<<" ConvertDone:"<<item.convert_done_waiting<<" StorageOps:"<<item.storage_ops_waiting<<" RetentionData:"<<item.retention_store_waiting<<" Purge:"<<item.purge_waiting<<" Whisper:"<<item.whisper_waiting<<" DBLoading:"<<(item.is_db_loading? "yes": "no") << std::endl;
			}
			
			lock.unlock();
		}
		else
			throw exception("Unable to gain lock");

		exit(0);
	}
	catch(const std::exception& e) {
		std::cerr << "Exception: "<<e.what()<<'\n';
	}
	
	exit(1);
}


