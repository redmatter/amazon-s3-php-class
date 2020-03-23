/*
 * monitor.cpp
 *
 *  Created on: 19 Nov 2011
 *      Author: dkorah
 */

#include "../archived/monitor.hpp"
#include "../common/archived/stats/averaged_item.hpp"

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

using common::archived::stats::averaged_item;

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

