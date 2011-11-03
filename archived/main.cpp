#include "globals.h"
#include "put_session.hpp"
#include "put_server.hpp"
#include "storage.hpp"
#include "storage_operation.hpp"
#include "storage_runner.hpp"
#include "converter.hpp"
#include "dispatcher.hpp"
#include "database.hpp"
#include "cdrmunch_database.hpp"
#include "clear_cache.hpp"
#include "operation_data.hpp"
#include "whisper_client.hpp"
#include "retention_data_flusher.hpp"
#include "purge.hpp"

#include "../common/types.hpp"
#include "../common/cdr_dir_monitor.hpp"
#include "../common/dir_monitor.hpp"

#include <rml/logger.h>
#include <rml/utils.h>
#include <rml/thread_adapter.h>
#include <rml/config.h>
#include <rml/siginit.h>
#include <rml/daemon.h>

#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/ref.hpp>

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

namespace app {

class operation_data_dedup
{
	const operation_data::busy_set_t& busy_set_;
	database::internal& internal_db_;

public:
	operation_data_dedup(const operation_data::busy_set_t& busy_set, database::internal& internal_db) :
		busy_set_(busy_set), internal_db_(internal_db)
	{
	}

	inline bool is_loaded(operation_data::pointer p_opdata) const
	{
		const bool& is_busied(busy_set_.is_busied(p_opdata));
		LogDebug(p_opdata<<" is_archivable_loaded [busied]: "<<is_busied);

		return is_busied;
	}

	inline bool is_identifier_loaded(const archiving::archivable::file_type::file_type_t& type, const archiving::org_id_t& org, const std::string& uuid) const
	{
		return is_loaded(operation_data::pointer(new operation_data(operation_data::detail::pointer(new operation_data::detail(type, org, uuid)))));
	}

	inline operation_data::pointer get_loaded_instance(const operation_data::pointer& p_lookup) const
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

}

int main(int argc, char **argv) try
{
	bool initialized = false;

	// default initialization to catch any initialization error
	rml::logger::configure();

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
		app::coreapi::describe_config(optsDesc);
		app::database::internal::describe_config(optsDesc);
		app::database::retention::describe_config(optsDesc);
		app::database::cdrmunch::describe_config(optsDesc);
		app::clear_cache::describe_config(optsDesc);
		app::whisper_client::describe_config(optsDesc);
		app::retention_data_flusher::describe_config(optsDesc);
		app::purge::describe_config(optsDesc);

		if(cli_confMap.count("help-conf")) {
			std::cout << optsDesc << "\n";
			return 1;
		}

		if(1 != cli_confMap.count("hostname"))
			throw rml::exception("hostname not given");

		app::hostname = cli_confMap["hostname"].as<std::string>();

		try {
			rml::config::parse(CONFIG_FILE_PATH, optsDesc, app::confMap);

			// configure the logger
			rml::logger::configure(app::confMap, SYSLOG_APP_NAME);

			// a new day is a gift!
			LogAlways(DEFAULT_APP_LONG_NAME" v"<<version_number_string<<' '<<copyright_string);
			LogAlways("built on "<<version_date<<' '<<build_host);

			// set the signal handlers
			rml::siginit();
			rml::daemon::initialise(app::confMap, PID_FILE_PATH);
		}
		catch(const std::exception &e) {
			LogError("Initialisation Error - exception: "<<e.what());
			exit(rml::utils::exit_codes::initialization_error);
		}
	}

	try {

		app::database::internal internal_db(app::confMap);
		app::database::retention retention_db(app::confMap);
		app::database::cdrmunch cdrmunch_db(app::confMap);

		app::operation_data::busy_set_t operation_data_busy_set;
		app::operation_data_dedup operation_data_dedup(operation_data_busy_set, internal_db);

		app::operation_data::channel_t convert_channel(
				boost::bind(&app::operation_data::busy_set_t::add, boost::ref(operation_data_busy_set), _1),
				boost::bind(&app::operation_data::busy_set_t::is_busied, boost::ref(operation_data_busy_set), _1),
				0, "Convert");

		app::operation_data::channel_t convert_done_channel("ConvertDone");
		app::storage::operation_channel_t storage_operation_channel("StorageOps");
		app::operation_data::channel_t retention_data_channel("RetentionData");
		app::operation_data::channel_t purge_channel("Purge");
		app::whisper::channel_t whisper_channel("Whisper");

		app::clear_cache clear_cache(app::confMap, internal_db);

		boost::asio::io_service io_service;

		app::coreapi coreapi(app::confMap, boost::str(boost::format("%1%:%2%.%3%") % DEFAULT_APP_NAME % version_number[2] % version_number[3]), io_service);
		app::policy::cached_finder_t cached_policy_finder(
				boost::bind(&app::policy::finder, boost::ref(coreapi), _1), "CachedPolicyFinder");
		app::storage::cached_endpoint_finder_t cached_endpoint_finder(
				boost::bind(&app::storage::endpoint_finder, boost::ref(coreapi), _1), "CachedEndpointFinder");

		app::put_server put_server(app::confMap, convert_channel, whisper_channel,
				cached_policy_finder, cached_endpoint_finder, internal_db, cdrmunch_db, clear_cache, io_service);
		app::converter converter(convert_channel, convert_done_channel, internal_db, app::quit);
		app::dispatcher dispatcher(convert_done_channel, storage_operation_channel, retention_data_channel,
				internal_db, app::quit);

		app::retention_data_flusher retention_data_flusher(app::confMap, retention_data_channel, purge_channel, retention_db, app::quit);

		app::purge purge(purge_channel, internal_db, operation_data_busy_set, app::quit, app::confMap);

		app::storage::s3::storage s3storage(app::confMap);

		app::storage::s3::runner s3runner(s3storage, storage_operation_channel, app::quit);

		app::whisper_client whisper_client(io_service, whisper_channel,
				app::confMap["distiller_client.server_host"].as<std::string>(),
				app::confMap["distiller_client.server_port"].as<std::string>());

		bool have_more_to_load = true;
		internal_db.find_retriables(app::hostname, cached_policy_finder, boost::bind(&app::operation_data_dedup::is_loaded, boost::ref(operation_data_dedup), _1), cached_endpoint_finder, convert_channel, have_more_to_load);

		initialized = true;
		LogMessage("Initialisation complete");

		boost::thread_group threads;

		{
			unsigned int n_threads = std::max<unsigned int>( app::confMap["converter.threads"].as<unsigned int>(), app::converter::min_threads );
			LogMessage("Starting "<<n_threads<<" converter threads");
			while(0 < --n_threads)
				threads.create_thread(boost::bind(&app::converter::run, boost::ref(converter)));
		}

		threads.create_thread(boost::bind(&app::dispatcher::run, boost::ref(dispatcher)));

		{
			unsigned int n_threads = std::max<unsigned int>( app::confMap["s3_runner.threads"].as<unsigned int>(), app::storage::s3::runner::min_threads );
			LogMessage("Starting "<<n_threads<<" s3 threads");
			while(0 < --n_threads)
				threads.create_thread(boost::bind(&app::storage::s3::runner::run, boost::ref(s3runner)));
		}

		{
			unsigned int n_threads = ( 1 + std::max<unsigned int>( app::confMap["archived.service_threads"].as<unsigned int>(), 4 ));
			LogMessage("Starting "<<(n_threads-1)<<"+1 service threads");
			while(0 < --n_threads)
				threads.create_thread(boost::bind(&boost::asio::io_service::run, boost::ref(io_service)));
		}

		threads.create_thread(boost::bind(&app::retention_data_flusher::run, boost::ref(retention_data_flusher)));
		threads.create_thread(boost::bind(&app::purge::run, boost::ref(purge)));

		unsigned int ticker = 1;
		while(!app::quit) {
			if(app::confMap["app.tik_tok"].as<std::string>() == "true")
				LogDebug(((ticker%2) ? "tik": "tok"));

			//TODO monitoring stuff goes in here

			if(0 == (ticker % 20)) {
				cached_endpoint_finder.clear_unused(60);
				cached_policy_finder.clear_unused(10);
				ticker = 0;
			}

			if(have_more_to_load && (0 == (ticker % 20)))
				internal_db.find_retriables(app::hostname, cached_policy_finder, boost::bind(&app::operation_data_dedup::is_loaded, boost::ref(operation_data_dedup), _1), cached_endpoint_finder, convert_channel, have_more_to_load);


			if(!(ticker % 60))
				LogMessage("[statistics] Convert:"<<convert_channel.count()<<" ConvertDone:"<<convert_done_channel.count()<<" StorageOps:"<<storage_operation_channel.count()<<" RetentionData:"<<retention_data_channel.count()<<" Purge:"<<purge_channel.count()<<" Whisper:"<<whisper_channel.count()<<" DBLoading:"<<RML_YES_NO(have_more_to_load));

			ticker++;

			boost::this_thread::sleep(boost::posix_time::seconds(1));
		}

		LogMessage("Quit flagged ... stopping channels");

		convert_channel.set_finishing();
		convert_done_channel.set_finishing();
		storage_operation_channel.set_finishing();
		retention_data_channel.set_finishing();
		purge_channel.set_finishing();
		whisper_channel.set_finishing();

		coreapi.logout();

		io_service.stop();

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
