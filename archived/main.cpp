#include "globals.h"
#include "put_session.hpp"
#include "put_server.hpp"
// TODO #include "get_server.hpp"
#include "storage.hpp"
#include "call_buffering_recording.hpp"
#include "storage_operation.hpp"
#include "storage_runner.hpp"
#include "converter.hpp"
#include "dispatcher.hpp"
#include "database_internal.hpp"
#include "database_retention.hpp"
#include "database_cdrmunch.hpp"
#include "clear_cache.hpp"
#include "operation_data.hpp"
#include "whisper_client.hpp"
#include "retention_data_flusher.hpp"
#include "purge.hpp"
#include "monitor.hpp"
#include "coreapi_user.hpp"
#include "state_reactor.hpp"
#include "retention_purge_runner.hpp"
#include "fcgi_storage_access.hpp"
#include "org_policy_versions.hpp"

#include "../common/types.hpp"

#include <rml/fcgi/server.hpp>
#include <rml/logger.h>
#include <rml/utils.h>
#include <rml/thread_adapter.h>
#include <rml/hapi.hpp>
#include <rml/config.h>
#include <rml/siginit.h>
#include <rml/daemon.h>

#include <fcgio.h>

#include <cryptopp/cryptlib.h>
#include <cryptopp/base64.h>
#include <cryptopp/hex.h>

#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/ref.hpp>
#include <boost/chrono/system_clocks.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/named_condition.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>

#include <iostream>
#include <fstream>
#include <algorithm>

#include <sys/types.h>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#ifndef inline_
#define inline_ inline
#endif

#ifndef SYSLOG_APP_NAME
#define SYSLOG_APP_NAME "rm-archived"
#endif

#ifndef DEFAULT_APP_NAME
#define DEFAULT_APP_NAME "rmarchived"
#endif

#ifndef DEFAULT_APP_LONG_NAME
#define DEFAULT_APP_LONG_NAME DEFAULT_APP_NAME
#endif

#ifndef CONFIG_FILE_PATH
#define CONFIG_FILE_PATH "/etc/"DEFAULT_APP_NAME".conf"
#endif

#ifndef PID_FILE_PATH
#define PID_FILE_PATH "/var/run/archiving/"DEFAULT_APP_NAME".pid"
#endif

#ifndef DEFAULT_LOG_FILE_PATH
#define DEFAULT_LOG_FILE_PATH "/var/log/"DEFAULT_APP_NAME".log"
#endif

extern "C" {
extern void OS_ShutdownPending();
}

namespace app {

monitor::monitor_t::counter_t counter;
boost::mutex counter_mutex;

class operation_data_dedup
{
	const operation_data::busy_set_t& busy_set_;
	database::internal& internal_db_;

public:
	operation_data_dedup(const operation_data::busy_set_t& busy_set, database::internal& internal_db) :
		busy_set_(busy_set), internal_db_(internal_db)
	{
	}

	inline_ bool is_loaded(operation_data::pointer p_opdata) const
	{
		const bool& is_busied(busy_set_.is_busied(p_opdata));
		LogDebug(p_opdata<<" is_archivable_loaded [busied]: "<<is_busied);

		return is_busied;
	}

	inline_ bool is_identifier_loaded(const archiving::file_type::file_type_t& type, const archiving::org_id_t& org, const std::string& uuid) const
	{
		return is_loaded(operation_data::pointer(new operation_data(operation_data::detail::pointer(new operation_data::detail(type, org, uuid)))));
	}

	inline_ operation_data::pointer get_loaded_instance(const operation_data::pointer& p_lookup) const
	{
		return busy_set_.get_item(p_lookup);
	}
};

std::string hostname;

// the configuration map
boost::program_options::variables_map confMap;

// time to quit?
volatile bool quit = false;

// was HUPed
volatile bool wasHUPed = false;

inline void shutdown()
{
    quit = true;
    OS_ShutdownPending();
}

inline void LogBuildInfo()
{
	LogAlways(DEFAULT_APP_LONG_NAME" v"<<version_number_string<<' '<<copyright_string);
	LogAlways("built on "<<version_date<<' '<<build_host);
}

void sigint(int s) {
    LogMessage("SIGINT: Trying a clean exit");
    shutdown();
}

void sighup(int s) {
    LogVerbose("SIGHUPed");
    wasHUPed = true;

	LogBuildInfo();
}

void sigterm(int s) {
    LogMessage("SIGTERM: Trying a clean exit");
    shutdown();
}

/* save */
void sigusr1(int s) {
    LogMessage("SIGUSR1: Trying a clean exit");
    shutdown();
}

}

int main(int argc, char *argv[]) try
{
	bool initialized = false;

	// default initialization to catch any initialization error
	rml::logger::configure();

	rml::hapi::host_cache::initialise();

	{
		boost::program_options::variables_map cli_confMap;
		boost::program_options::options_description cli_optsDesc(
					boost::str(boost::format(
							DEFAULT_APP_LONG_NAME" v%1%"
							"\n%2%\n\nOptions"
							) % version_number_string % copyright_string));
		cli_optsDesc.add_options()
				("hostname", boost::program_options::value<std::string>(), "this hostname")
				("help-conf", "conf file help")
				("help", "this help")
				("version", "shows version information")
				;
		boost::program_options::store(boost::program_options::parse_command_line(argc, argv, cli_optsDesc), cli_confMap);
		boost::program_options::notify(cli_confMap);

		if (cli_confMap.count("help")) {
			std::cout << cli_optsDesc << "\n";
			return 1;
		}

		boost::program_options::options_description optsDesc(boost::str( boost::format(DEFAULT_APP_LONG_NAME" v%1%\n%2%\noptions") % version_number_string % copyright_string ));
		optsDesc.add_options()
				("archived.service_threads",  boost::program_options::value<unsigned int>()->default_value(4), "ArchiveD service threads")
				("app.tik_tok", boost::program_options::value<std::string>()->default_value(std::string("false")), "enable tik tok")
				;
		rml::daemon::describe_config(optsDesc);
		rml::logger::describe_config(optsDesc);

		app::put_session::describe_config(optsDesc);
		app::put_server::describe_config(optsDesc);
		app::converter::describe_config(optsDesc);
		app::storage::s3::backend::describe_config(optsDesc);
		app::storage::s3::runner::describe_config(optsDesc);
		rml::coreapi::describe_config(optsDesc);
		app::database::internal::describe_config(optsDesc);
		app::database::retention::describe_config(optsDesc);
		app::database::retention_delete::describe_config(optsDesc);
		app::database::cdrmunch::describe_config(optsDesc);
		app::clear_cache::describe_config(optsDesc);
		app::whisper_client::describe_config(optsDesc);
		app::retention_data_flusher::describe_config(optsDesc);
		app::purge::describe_config(optsDesc);
		rml::fcgi::server::describe_config(optsDesc);
		app::call_buffering_recording::describe_config(optsDesc);
		app::state_reactor<>::describe_config(optsDesc);
		app::retention_purge::describe_config(optsDesc);

		if(cli_confMap.count("help-conf")) {
			std::cout << optsDesc << "\n";
			exit(rml::utils::exit_codes::normal);
		}

		if(cli_confMap.count("version")) {
			std::cout << boost::str( boost::format(DEFAULT_APP_LONG_NAME" v%1%\n%2%\n") % version_number_string % copyright_string );
			exit(rml::utils::exit_codes::normal);
		}

		if(1 != cli_confMap.count("hostname"))
			throw rml::exception("hostname not given");

		app::hostname = cli_confMap["hostname"].as<std::string>();

		try {
			rml::config::parse(CONFIG_FILE_PATH, optsDesc, app::confMap);

			// configure the logger
			rml::logger::configure(app::confMap, SYSLOG_APP_NAME);

			app::LogBuildInfo();

			// set the signal handlers
            rml::siginit(4,
                        SIGINT, app::sigint,
                        SIGHUP, app::sighup,
                        SIGTERM, app::sigterm,
                        SIGUSR1, app::sigusr1);
            rml::daemon::initialise(app::confMap, PID_FILE_PATH);
		}
		catch(const std::exception &e) {
			LogError("Initialisation Error - exception: "<<e.what());
			exit(rml::utils::exit_codes::initialization_error);
		}

		// to avoid race condition arising from lack of thread-safety in code
		CryptoPP::HexDecoder hexDecoder; (void)hexDecoder;
		CryptoPP::HexEncoder hexEncoder(0, false); (void)hexEncoder;
		CryptoPP::Base64Encoder b64Encoder; (void)b64Encoder;
		CryptoPP::Base64Decoder b64Decoder; (void)b64Decoder;
	}

	try {

		app::database::internal internal_db(app::confMap);
		app::database::retention retention_db(app::confMap);
		app::database::retention_delete retention_delete_db(app::confMap);
		app::database::cdrmunch cdrmunch_db(app::confMap);

		app::operation_data::busy_set_t operation_data_busy_set;
		app::operation_data_dedup operation_data_dedup(operation_data_busy_set, internal_db);

		app::operation_data::channel_t convert_channel(
				boost::protect(boost::bind(&app::operation_data::busy_set_t::add, boost::ref(operation_data_busy_set), _1)),
				boost::protect(boost::bind(&app::operation_data::busy_set_t::is_busied, boost::ref(operation_data_busy_set), _1)),
				0, "Convert");

		app::operation_data::channel_t convert_done_channel("ConvertDone");
		app::storage::operation_channel_t storage_operation_channel("StorageOps");
		app::operation_data::channel_t retention_data_channel("RetentionData");
		app::operation_data::channel_t purge_channel("Purge");
		app::whisper::channel_t whisper_channel("Whisper");
		app::call_buffering_recording::channel_t cbr_channel("CallBuffering");

		app::clear_cache clear_cache(app::confMap, internal_db);

		boost::shared_ptr< boost::asio::io_service > p_io_service = boost::make_shared< boost::asio::io_service >();
		boost::asio::io_service::work work(*p_io_service); // make sure we keep it proped up!
		
		rml::coreapi coreapi(app::confMap, boost::str(boost::format("%1%:%2%.%3%") % DEFAULT_APP_NAME % version_number[2] % version_number[3]), p_io_service);
		app::policy::cached_finder_t cached_policy_finder(
				boost::bind(&app::policy::finder, boost::ref(coreapi), _1), "CachedPolicyFinder");
		app::storage::cached_endpoint_finder_t cached_endpoint_finder(
				boost::bind(&app::storage::endpoint_finder, boost::ref(coreapi), _1), "CachedEndpointFinder");

		app::retention_record::cached_finder_t cached_retention_record_finder(
				boost::bind(&app::database::retention::get_retention, boost::ref(retention_db), _1), "CachedRetentionRecordFinder", 1, 2, 10);

		app::coreapi_user::cached_finder_t cached_coreapi_user_finder(
				boost::bind(app::coreapi_user::find, boost::ref(coreapi), _1), "CachedUserFinder", 1, 2, 10);

		app::org_policy_versions::cached_finder_t cached_org_policy_versions_finder(
				boost::bind(app::org_policy_versions::find_from_api, boost::ref(coreapi), _1), "CachedOrgPolicyVersionsFinder");

		app::retention_record::fn_is_expired_t retention_record_is_expired(
				boost::bind(app::retention_record::is_expired, boost::ref(cached_policy_finder), _1));

		app::coreapi_eventlog::cached_finder_t cached_coreapi_eventlog_finder(
				boost::bind(app::coreapi_eventlog::find, boost::ref(coreapi), _1), "CachedEventLogFinder", 1, 2, 10);

		app::put_server put_server(app::confMap, convert_channel, whisper_channel, cbr_channel,
				cached_policy_finder, cached_endpoint_finder, internal_db, cdrmunch_db, clear_cache, *p_io_service);
		app::converter converter(convert_channel, convert_done_channel, operation_data_busy_set, internal_db, app::quit);
		app::dispatcher dispatcher(convert_done_channel, storage_operation_channel, retention_data_channel,
				internal_db, cached_endpoint_finder, app::quit);

		app::retention_data_flusher retention_data_flusher(app::confMap, retention_data_channel, purge_channel, retention_db, app::quit);

		app::purge purge(purge_channel, internal_db, operation_data_busy_set, app::quit, app::confMap);

		app::storage::s3::storage s3storage(app::confMap);

		app::storage::s3::runner s3runner(s3storage, storage_operation_channel, app::quit);

		boost::shared_ptr< app::state_reactor<> > p_purge_state_reactor(new app::state_reactor<>(app::confMap));
		boost::shared_ptr<app::retention_purge::runner> p_retention_purge_runner(
				new app::retention_purge::runner(
						app::confMap, cached_endpoint_finder, cached_policy_finder, cached_org_policy_versions_finder,
						coreapi, *p_purge_state_reactor, retention_db, retention_delete_db, internal_db, app::quit));

		app::whisper_client whisper_client(*p_io_service, whisper_channel,
				app::confMap["distiller_client.server_host"].as<std::string>(),
				app::confMap["distiller_client.server_port"].as<std::string>());

		bool have_more_to_load = true;
		internal_db.find_retriables(app::hostname, cached_policy_finder, boost::bind(&app::operation_data_dedup::is_loaded, boost::ref(operation_data_dedup), _1), cached_endpoint_finder, convert_channel, have_more_to_load);

		struct shm_remove
		{
			shm_remove()
			{
				boost::interprocess::shared_memory_object::remove(SHM_NAME);
			}
			
			~shm_remove()
			{
				boost::interprocess::shared_memory_object::remove(SHM_NAME);
			}
		} remover;
		
		boost::interprocess::shared_memory_object shm(boost::interprocess::create_only, SHM_NAME, boost::interprocess::read_write);

		shm.truncate(sizeof(app::monitor::monitor_t));
		boost::interprocess::mapped_region region(shm, boost::interprocess::read_write);
		app::monitor::monitor_t *monitor_data = new (region.get_address()) app::monitor::monitor_t;
		
		rml::fcgi::server fcgi_server(app::confMap, app::quit);
		fcgi_server.register_handler("/", boost::bind(app::fcgi::storage_access_handler::make,
				boost::ref(cached_retention_record_finder), boost::ref(retention_record_is_expired),
				boost::ref(cached_policy_finder), boost::ref(cached_coreapi_user_finder),
				boost::ref(cached_endpoint_finder), boost::ref(cached_coreapi_eventlog_finder), _1, _2));
		
		// todo add ondemand purge notification handler


		initialized = true;
		LogMessage("Initialisation complete");

		boost::thread_group threads;

		fcgi_server.start(threads);

		{
			unsigned int n_threads = std::max<unsigned int>( app::confMap["converter.threads"].as<unsigned int>(), app::converter::min_threads );
			LogMessage("Starting "<<n_threads<<" converter threads");
			while(0 < n_threads--)
				threads.create_thread(boost::bind(&app::converter::run, boost::ref(converter)));
		}

		threads.create_thread(boost::bind(&app::dispatcher::run, boost::ref(dispatcher)));

		{
			unsigned int n_threads = std::max<unsigned int>( app::confMap["s3_runner.threads"].as<unsigned int>(), app::storage::s3::runner::min_threads );
			LogMessage("Starting "<<n_threads<<" s3 threads");
			while(0 < n_threads--)
				threads.create_thread(boost::bind(&app::storage::s3::runner::run, boost::ref(s3runner)));
		}

		{
			unsigned int n_threads = ( 1 + std::max<unsigned int>( app::confMap["archived.service_threads"].as<unsigned int>(), 4 ));
			LogMessage("Starting "<<(n_threads-1)<<"+1 service threads");
			while(0 < n_threads--)
				threads.create_thread(boost::bind(&boost::asio::io_service::run, p_io_service));
		}

		threads.create_thread(boost::bind(&app::retention_data_flusher::run, boost::ref(retention_data_flusher)));
		threads.create_thread(boost::bind(&app::purge::run, boost::ref(purge)));
		threads.create_thread(boost::bind(
				app::call_buffering_recording::run, boost::ref(app::confMap), boost::ref(cached_endpoint_finder),
				boost::ref(cbr_channel), app::quit));
		threads.create_thread(boost::bind(&app::retention_purge::runner::feed_purge_queue, p_retention_purge_runner));

		put_server.start();

		bool monitor_pending(false);
		bool more_to_reload(true);

		unsigned int ticker = 1;
		while(!app::quit) {
			if(app::confMap["app.tik_tok"].as<std::string>() == "true")
				LogDebug(((ticker%2) ? "tik": "tok"));

			if(0 == (ticker % 20)) {
				LogMessage("Clearing internal caches");
				cached_endpoint_finder.clear_unused(60);
				cached_policy_finder.clear_unused(10);
				cached_org_policy_versions_finder.clear_unused(10);
				cached_retention_record_finder.clear_unused(10);
				cached_coreapi_user_finder.clear_unused(10);
			}

			if(have_more_to_load && (0 == (ticker % 20))) {
				LogMessage("Loading more items from ArchiveDQueue");
				internal_db.find_retriables(app::hostname, cached_policy_finder,
											boost::bind(&app::operation_data_dedup::is_loaded,
														boost::ref(operation_data_dedup), _1),
											cached_endpoint_finder, convert_channel, have_more_to_load);
			}

			// load approved purge items occasionally
			if(more_to_reload && (0 == (ticker % std::min<unsigned int>(180, app::confMap["retention_purge.approval_run_interval_minutes"].as<unsigned int>() * 60)))) {
				LogMessage("Loading more purge items from ArchiveDPurgeQueue");
				more_to_reload = p_retention_purge_runner->reload_from_database();
			}

			// occasionally save items to DB
			if(0 == (ticker % app::confMap["retention_purge.db_flush_interval_seconds"].as<unsigned int>())) {
				LogMessage("Saving progress of purge items");
				const boost::posix_time::ptime& batch_save_run_till(
						boost::posix_time::microsec_clock::universal_time() + boost::posix_time::seconds(15));
				internal_db.batch_save_run<app::retention_purge::item, app::retention_purge::item::weak_set_t>(
						batch_save_run_till);
			}

			if(monitor_pending || !(ticker % 60)) {
				LogMessage("Preparing statistics for the past minute");
				boost::posix_time::ptime till = boost::posix_time::microsec_clock::local_time() + boost::posix_time::milliseconds(50);
				boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> lock(monitor_data->mutex, till);
				
				if(lock) {
					LogMessage("Adding statistics for the past minute");
					app::monitor::monitor_t::item_t item;
					time(&item.sample_time);
					item.convert_waiting = convert_channel.count();
					item.convert_done_waiting = convert_done_channel.count();
					item.storage_ops_waiting = storage_operation_channel.count();
					item.retention_store_waiting = retention_data_channel.count();
					item.purge_waiting = purge_channel.count();
					item.whisper_waiting = whisper_channel.count();
					
					item.is_db_loading = have_more_to_load;
					
					item.counter = app::monitor::get_counter(true);

					monitor_data->add_item(item);
					
					LogMessage("[statistics] "<<item);
					
					monitor_pending = false;
				}
				else {
					LogMessage("Could not add statistics for the past minute [failed to lock]. Trying again in a second");
					monitor_pending = true;
				}
			}

			ticker++;

			boost::this_thread::sleep(boost::posix_time::seconds(1));
		}

		LogMessage("Quit flagged ... stopping channels");

		(void)put_server.stop();
		
		convert_channel.set_finishing();
		convert_done_channel.set_finishing();
		storage_operation_channel.set_finishing();
		retention_data_channel.set_finishing();
		purge_channel.set_finishing();
		whisper_channel.set_finishing();
		cbr_channel.set_finishing();

		p_purge_state_reactor.reset();
		p_retention_purge_runner->shutdown();

		coreapi.logout();

		p_io_service->stop();

		fcgi_server.stop();

		threads.join_all();

		LogAlways("terminated normally");

		rml::logger::deconfigure();

		exit(rml::utils::exit_codes::normal);
	}
	catch(const std::exception &e) {
		LogError("exception: "<<e.what());
		LogMessage("Abnormal program termination");
		exit(initialized? rml::utils::exit_codes::failure: rml::utils::exit_codes::initialization_error);
	}
}
catch(const std::exception& e) {
	LogError("Exception: "<<e.what());
	LogMessage("Abnormal program termination");
	exit(rml::utils::exit_codes::failure);
}
catch(...) {
	LogError("Unexpected exception");
	LogMessage("Abnormal program termination");
	exit(rml::utils::exit_codes::failure);
}

