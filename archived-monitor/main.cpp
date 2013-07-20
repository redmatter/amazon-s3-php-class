/*
 * monitor.cpp
 *
 *  Created on: 19 Nov 2011
 *      Author: dkorah
 */

#include "../archived/monitor.hpp"

#include <boost/program_options.hpp>
#include <boost/bind.hpp>
#include <boost/format.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/date_time.hpp>

#include <iostream>

#include <signal.h>

// from $(TMP)/version.c
extern "C" {
extern int version_number[4];
extern const char *version_number_string;
extern const char *version_number_only_string;
extern const char *version_date;
extern const char *build_host;
extern const char *copyright_string;
}

#ifndef DEFAULT_APP_LONG_NAME
#define DEFAULT_APP_LONG_NAME "rmarchived-monitor"
#endif

namespace app
{
monitor::monitor_t::counter_t counter;
boost::mutex counter_mutex;
}

namespace {

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
	float sample_count;
	
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
	float counter_call_buffering;
	
	averaged_item(const float n_samples) :
		n_samples(n_samples),
		sample_time(0), 
		convert_waiting(0.0), convert_done_waiting(0.0), storage_ops_waiting(0.0), retention_store_waiting(0.0), purge_waiting(0.0), whisper_waiting(0.0), 
		is_db_loading(false), 
		store_put_count(0.0), store_put_size(0.0), store_convert_count(0.0), store_save_count(0.0), store_retention_count(0.0),
		counter_put(0.0), counter_cached_only(0.0), counter_stored(0.0), counter_stored_size(0.0), counter_call_buffering(0.0)
	{
	}

	void add_sample(const app::monitor::monitor_t::item_t& i)
	{
		if(sample_count >= n_samples)
			return;
		
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
		counter_call_buffering += (i.counter.call_buffering / n_samples);
		
		sample_count++;
	}
	
	friend std::ostream& operator<< (std::ostream& os, const averaged_item& item)
	{
		if(item.sample_count != item.n_samples)
			return os;
		
		return os << int(item.n_samples)<<" mins: "<<item.sample_time<<" Convert:"<<item.convert_waiting<<" ConvertDone:"<<item.convert_done_waiting<<" StorageOps:"<<item.storage_ops_waiting<<" RetentionData:"<<item.retention_store_waiting<<" Purge:"<<item.purge_waiting<<" Whisper:"<<item.whisper_waiting<<" DBLoading:"<<(item.is_db_loading? "yes": "no")<<" counters:"<<item.counter_put<<'/'<<item.counter_cached_only<<'/'<<item.counter_stored<<' '<<item.counter_stored_size<<" CallBuffering:"<<item.counter_call_buffering << std::endl;
	}
};

struct averaged_item_cacti : public averaged_item
{
	averaged_item_cacti() : averaged_item(5.0)
	{
	}
	
	friend std::ostream& operator<< (std::ostream& os, const averaged_item_cacti& item)
	{
		if(item.sample_count != item.n_samples)
			return os << "ConvertQueue:0.00 ConvertDoneQueue:0.00 StorageOpsQueue:0.00 RetentionDataQueue:0.00 PurgeQueue:0.00 WhisperQueue:0.00 PutCount:0.00 CachedOnlyCount:0.00 StoredCount:0.00 StoredBytes:0.00" << std::endl;
		
		return os << "ConvertQueue:"<<item.convert_waiting<<" ConvertDoneQueue:"<<item.convert_done_waiting<<" StorageOpsQueue:"<<item.storage_ops_waiting<<" RetentionDataQueue:"<<item.retention_store_waiting<<" PurgeQueue:"<<item.purge_waiting<<" WhisperQueue:"<<item.whisper_waiting<<" PutCount:"<<item.counter_put<<" CachedOnlyCount:"<<item.counter_cached_only<<" StoredCount:"<<item.counter_stored<<" StoredBytes:"<<item.counter_stored_size<<" CallBuffering:"<<item.counter_call_buffering << std::endl;
	}
};

class sentry : public boost::noncopyable
{
public:
	typedef boost::function<void()> fn_t;

	/**
	 * Setup the sentry
	 * @param fn
	 */
	sentry(const fn_t& fn) : fn_(fn), active_(true)
	{
	}

	/**
	 * Hey sentry; do something; we are going down
	 */
	~sentry()
	{
		if(active_)
			fn_();
	}

	void cancel()
	{
		active_ = false;
	}

private:
	fn_t fn_;
	bool active_;
};

} // anonymous namespace

int main(int argc, char* argv[])
{
	signal(SIGPIPE, SIG_IGN);

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
				("cacti", "show cacti friendly output")
				("samples", "show all samples")
				("help", "this help")
				("version", "shows version information")
				;
		boost::program_options::store(boost::program_options::parse_command_line(argc, argv, cli_optsDesc), cli_confMap);
		boost::program_options::notify(cli_confMap);

		if (cli_confMap.count("help")) {
			std::cout << cli_optsDesc << "\n";
			exit(0);
		}
		
		if(cli_confMap.count("version")) {
			std::cout << boost::str( boost::format(DEFAULT_APP_LONG_NAME" v%1%\n%2%\n") % version_number_string % copyright_string );
			exit(0);
		}

		const bool show_averages((cli_confMap.count("averages") != 0) || ((cli_confMap.count("samples") == 0) && (cli_confMap.count("cacti") == 0)));
		const bool show_samples(cli_confMap.count("samples") != 0);
		const bool show_cacti(cli_confMap.count("cacti") != 0);

		std::cout.precision(2);
		std::cout.setf(std::ios::fixed, std::ios::floatfield);
		
		boost::interprocess::shared_memory_object shm(boost::interprocess::open_only, SHM_NAME, boost::interprocess::read_write);

		boost::interprocess::mapped_region region(shm, boost::interprocess::read_write);
		app::monitor::monitor_t* monitor_data = static_cast<app::monitor::monitor_t*>(region.get_address());
		
		boost::posix_time::ptime till = boost::posix_time::microsec_clock::local_time() + boost::posix_time::milliseconds(500);
		boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> lock(monitor_data->mutex, till);
		if(lock) {
			sentry lock_sentry(boost::bind(&boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex>::unlock, boost::ref(lock)));
			
			averaged_item_cacti cacti;
			averaged_item _1(1.0), _3(3.0), _10(10.0), _30(30.0), _60(60.0);
			
			if(!monitor_data->n_items)
				throw exception("No items");
			
			for(size_t i = 0; i < monitor_data->n_items; ++i) {
				const app::monitor::monitor_t::item_t& item(monitor_data->items[i]);
				if(show_samples)
					std::cout << item << std::endl;
				
				if(show_cacti && monitor_data->n_items >= 5 && i < 5)
					cacti.add_sample(item);
				
				if(show_averages) {
					_1.add_sample(item);
					_3.add_sample(item);
					_10.add_sample(item);
					_30.add_sample(item);
					_60.add_sample(item);
				}
			}
			
			if(show_averages) {
				std::cout << _1;
				std::cout << _3;
				std::cout << _10;
				std::cout << _30;
				std::cout << _60;
			}
			
			if(show_cacti)
				std::cout << cacti;
		}
		else
			throw exception("Unable to gain lock");

		exit(0);
	}
	catch(const std::exception& e) {
		std::cerr << e.what()<<'\n';
	}
	
	exit(1);
}

