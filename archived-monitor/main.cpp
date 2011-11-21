/*
 * monitor.cpp
 *
 *  Created on: 19 Nov 2011
 *      Author: dkorah
 */

#include "../archived/monitor.hpp"

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/date_time.hpp>

#include <iostream>

// from $(TMP)/version.c
extern "C" {
extern int version_number[3];
extern const char *version_number_string;
extern const char *version_number_only_string;
extern const char *version_date;
extern const char *build_host;
extern const char *copyright_string;
}

#ifndef DEFAULT_APP_LONG_NAME
#define DEFAULT_APP_LONG_NAME "rmarchived-monitor"
#endif

class exception : public std::exception {
private:
	const std::string m_err;
public:
	exception (const std::string &err) : m_err(err) {}
	virtual const char* what() const throw() { return &m_err[0]; }
	~exception() throw () {}
};

struct averaged_item
{
	const float n_samples;
	
	time_t sample_time;

	float convert_waiting;
	float convert_done_waiting;
	float storage_ops_waiting;
	float retention_store_waiting;
	float purge_waiting;
	float whisper_waiting;

	bool is_db_loading;

	float store_put_count;
	float store_put_size;
	float store_convert_count;
	float store_save_count;
	float store_retention_count;

	float counter_put;
	float counter_cached_only;
	float counter_stored;
	float counter_stored_size;
	
	averaged_item(const float n_samples) :
		n_samples(n_samples),
		sample_time(0), 
		convert_waiting(0.0), convert_done_waiting(0.0), storage_ops_waiting(0.0), retention_store_waiting(0.0), purge_waiting(0.0), whisper_waiting(0.0), 
		is_db_loading(false), 
		store_put_count(0.0), store_put_size(0.0), store_convert_count(0.0), store_save_count(0.0), store_retention_count(0.0),
		counter_put(0.0), counter_cached_only(0.0), counter_stored(0.0), counter_stored_size(0.0)
	{
	}

	void add_sample(const app::monitor::monitor_t::item_t& i)
	{
		if(!sample_time) {
			sample_time = i.sample_time;
			is_db_loading = i.is_db_loading;
		}
		
		convert_waiting += (i.convert_waiting / n_samples);
		
		convert_done_waiting += (i.convert_done_waiting / n_samples);
		storage_ops_waiting += (i.storage_ops_waiting / n_samples);
		retention_store_waiting += (i.retention_store_waiting / n_samples);
		purge_waiting += (i.purge_waiting / n_samples);
		whisper_waiting += (i.whisper_waiting / n_samples);

		store_put_count += (i.store_put_count / n_samples);
		store_put_size += (i.store_put_size / n_samples);
		store_convert_count += (i.store_convert_count / n_samples);
		store_save_count += (i.store_save_count / n_samples);
		store_retention_count += (i.store_retention_count / n_samples);

		counter_put += (i.counter.put / n_samples);
		counter_cached_only += (i.counter.cached_only / n_samples);
		counter_stored += (i.counter.stored / n_samples);
		counter_stored_size += (i.counter.stored_size / n_samples);
	}
	
	friend std::ostream& operator<< (std::ostream& os, const averaged_item& item)
	{
		return os << int(item.n_samples)<<" mins: "<<item.sample_time<<" Convert:"<<item.convert_waiting<<" ConvertDone:"<<item.convert_done_waiting<<" StorageOps:"<<item.storage_ops_waiting<<" RetentionData:"<<item.retention_store_waiting<<" Purge:"<<item.purge_waiting<<" Whisper:"<<item.whisper_waiting<<" DBLoading:"<<(item.is_db_loading? "yes": "no")<<" counters:"<<item.counter_put<<'/'<<item.counter_cached_only<<'/'<<item.counter_stored<<' '<<item.counter_stored_size;
	}
};

int main(int argc, char* argv[])
{
	try
	{
		boost::program_options::variables_map cli_confMap;
		boost::program_options::options_description cli_optsDesc(
					boost::str(boost::format(
							DEFAULT_APP_LONG_NAME" v%1%"
							"\n%2%\n\nOptions"
							) % version_number_string % copyright_string));
		cli_optsDesc.add_options()
				("averages", "show averages")
				("samples", "show all samples")
				("help", "this help")
				;
		boost::program_options::store(boost::program_options::parse_command_line(argc, argv, cli_optsDesc), cli_confMap);
		boost::program_options::notify(cli_confMap);

		if (cli_confMap.count("help")) {
			std::cout << cli_optsDesc << "\n";
			return 1;
		}
		
		const bool show_averages((cli_confMap.count("averages") != 0) || (cli_confMap.count("samples") == 0));
		const bool show_samples(cli_confMap.count("samples") != 0);

		std::cout.precision(2);
		std::cout.setf(std::ios::fixed, std::ios::floatfield);
		
		boost::interprocess::shared_memory_object shm(boost::interprocess::open_only, SHM_NAME, boost::interprocess::read_write);

		boost::interprocess::mapped_region region(shm, boost::interprocess::read_write);
		app::monitor::monitor_t* monitor_data = static_cast<app::monitor::monitor_t*>(region.get_address());
		
		boost::posix_time::ptime till = boost::posix_time::microsec_clock::local_time() + boost::posix_time::milliseconds(500);
		boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> lock(monitor_data->mutex, till);
		if(lock) {
			
			averaged_item _3(3.0), _10(10.0), _30(30.0), _60(60.0);
			
			// try catch to be doubly sure we wouldnt leave the mutex locked! fudge..!
			try {
				if(!monitor_data->n_items)
					throw exception("No items");
				
				for(size_t i = 0; i < monitor_data->n_items; ++i) {
					const app::monitor::monitor_t::item_t& item(monitor_data->items[i]);
					if(show_samples)
						std::cout << item << std::endl;
					
					if(show_averages) {
						if(monitor_data->n_items >= 3 && i < 3)
							_3.add_sample(item);
						
						if(monitor_data->n_items >= 10 && i < 10)
							_10.add_sample(item);
						
						if(monitor_data->n_items >= 30 && i < 30)
							_30.add_sample(item);
						
						if(monitor_data->n_items >= 60 && i < 60)
							_60.add_sample(item);
					}
				}
			}
			catch(const std::exception& e) {
				std::cerr << "Exception: "<<e.what()<<'\n';				
			}
			
			lock.unlock();
			
			if(show_averages) {
				std::cout << _3 << std::endl;
				std::cout << _10 << std::endl;
				std::cout << _30 << std::endl;
				std::cout << _60 << std::endl;
			}
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


