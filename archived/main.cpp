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

#include "../common/types.hpp"
#include "../common/cdr_dir_monitor.hpp"
#include "../common/dir_monitor.hpp"

#include <rml/logger.h>
#include <rml/utils.h>
#include <rml/thread_adapter.h>
#include <rml/config.h>
#include <rml/siginit.h>

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

std::string hostname;

// the configuration map
boost::program_options::variables_map confMap;

// time to quit?
volatile bool quit = false;

// was HUPed
volatile bool wasHUPed = false;


std::string operation_data::temp_files_base_directory;

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
				("app.run_as_user", boost::program_options::value<std::string>()->default_value(std::string("")), "user to run the daemon as")
				("app.run_as_group", boost::program_options::value<std::string>()->default_value(std::string("")), "group to run the daemon as")
				("app.max_core_size_mega_bytes", boost::program_options::value<unsigned long>()->default_value(300UL), "max core size in mega bytes")
				("app.data_dir", boost::program_options::value<std::string>()->default_value(std::string("/var/lib/archiving")), "data directory")
				("archived.service_threads",  boost::program_options::value<unsigned int>()->default_value(4), "ArchiveD service threads")
				("app.tik_tok", boost::program_options::value<std::string>()->default_value(std::string("false")), "enable tik tok")
				;

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
		app::operation_data::describe_config(optsDesc);
		app::whisper_client::describe_config(optsDesc);
		app::retention_data_flusher::describe_config(optsDesc);

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

			// set core limit
			{
				struct rlimit rlim;
				if(getrlimit(RLIMIT_CORE, &rlim) == 0)
				{
					unsigned long core_limit = app::confMap["app.max_core_size_mega_bytes"].as<unsigned long>();
					LogVerbose("Setting core limit from " << rlim.rlim_cur << "B to " << core_limit << 'M' );

					rlim.rlim_cur = core_limit * ( 1024 * 1024 );
					if(0 != setrlimit(RLIMIT_CORE,&rlim))
						LogError("Could not enable core - err:"<<strerror(errno));
				}
				else
					LogError("Could not getrlimit - err:"<<strerror(errno));
			}

			// change user & group
			{
				std::string runas_user = app::confMap["app.run_as_user"].as<std::string>();
				std::string runas_group = app::confMap["app.run_as_group"].as<std::string>();

				std::string data_dir = app::confMap["app.data_dir"].as<std::string>();

				uid_t runas_uid = 0;
				gid_t runas_gid = 0;

				if(!runas_user.empty()) {
					struct passwd *pw = getpwnam(runas_user.c_str());
					if(!pw) {
						LogCritical("User " << runas_user << " not found on system");
						throw rml::exception("Configuration error - cdrpostd.run_as_user improper");
					}

					runas_uid = pw->pw_uid;
					if(pw->pw_dir && strlen(pw->pw_dir))
						data_dir = pw->pw_dir;

					if(runas_group.empty())
						runas_gid = pw->pw_gid;
				}

				if(!runas_group.empty()) {
					struct group *gr = getgrnam(runas_group.c_str());
					if(!gr) {
						LogCritical("Group " << runas_group << " not found on system");
						throw rml::exception("Configuration error - cdrpostd.runas_group improper");
					}
					runas_gid = gr->gr_gid;
				}

				if(!runas_group.empty()) {
					if(setgid(runas_gid)) {
						LogCritical("Unable to set gid to " << runas_group << "(gid:" << runas_gid << ") err: " << strerror(errno));
						throw rml::exception("setgid failed");
					}

					if(initgroups(runas_user.c_str(), runas_gid))
						LogError("Unable to set initgroups() with gid " << runas_gid);
				}

				if(!runas_user.empty()) {
					if(setuid(runas_uid)) {
						LogCritical("Unable to set uid to " << runas_user << "(uid:" << runas_uid << ") err: " << strerror(errno));
						throw rml::exception("setuid failed");
					}
				}

				// change dir so that we have a chance of getting some cores
				if(-1 == chdir(data_dir.c_str()))
					THROW_RML_EXCEPTION("Failed changing directory to " << data_dir << " - err:" << strerror(errno));
				else
					LogVerbose("Changed directory to " << data_dir);
			}

			// create / update pid file
			rml::utils::pid_file::create(PID_FILE_PATH);
		}
		catch(const std::exception &e) {
			LogError("Initialisation Error - exception: "<<e.what());
			exit(rml::utils::exit_codes::initialization_error);
		}
	}

	try {
		app::operation_data::temp_files_base_directory = app::confMap["temp_files.base_directory"].as<std::string>();

		app::operation_data::channel_t convert_channel("Convert");
		app::operation_data::channel_t convert_done_channel("ConvertDone");
		app::storage::operation_channel_t storage_operation_channel("StorageOps");
		app::operation_data::channel_t retention_data_channel("RetentionData");
		app::operation_data::channel_t usage_data_channel("UsageData");
		app::whisper::channel_t whisper_channel("Whisper");

		app::database::internal internal_db(app::confMap);
		app::database::retention retention_db(app::confMap);
		app::database::cdrmunch cdrmunch_db(app::confMap);

		app::clear_cache clear_cache(app::confMap, internal_db);

		boost::asio::io_service io_service;

		app::coreapi coreapi(app::confMap, boost::str(boost::format("%1%:%2%.%3%") % DEFAULT_APP_NAME % version_number[2] % version_number[3]), io_service);
		app::policy::cached_finder_t cached_policy_finder(
				boost::bind(&app::policy::finder, boost::ref(coreapi), _1));
		app::storage::cached_endpoint_finder_t cached_endpoint_finder(
				boost::bind(&app::storage::endpoint_finder, boost::ref(coreapi), _1));

		app::put_server put_server(app::confMap, convert_channel, whisper_channel,
				cached_policy_finder, cached_endpoint_finder, internal_db, cdrmunch_db, clear_cache, io_service);
		app::converter converter(convert_channel, convert_done_channel, internal_db, app::quit);
		app::dispatcher dispatcher(convert_done_channel, storage_operation_channel, retention_data_channel,
				internal_db, app::quit);

		app::retention_data_flusher retention_data_flusher(app::confMap, retention_data_channel, retention_db, app::quit);

		app::storage::s3::storage s3storage(app::confMap);

		app::storage::s3::runner s3runner(s3storage, storage_operation_channel, app::quit);

		app::whisper_client whisper_client(io_service, whisper_channel,
				app::confMap["distiller_client.server_host"].as<std::string>(),
				app::confMap["distiller_client.server_port"].as<std::string>());

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

		unsigned int ticker = 1;
		while(!app::quit) {
			if(app::confMap["app.tik_tok"].as<std::string>() == "true")
				LogDebug(((ticker%2) ? "tik": "tok"));

			//TODO monitoring stuff goes in here

			if(0 == (ticker % 20)) {
				cached_endpoint_finder.clear_unused(300);
				cached_policy_finder.clear_unused(300);
				ticker = 0;
			}

			ticker++;

			boost::this_thread::sleep(boost::posix_time::seconds(1));
		}

		LogMessage("Quit flagged ... stopping channels");

		convert_channel.set_finishing();
		convert_done_channel.set_finishing();
		storage_operation_channel.set_finishing();
		retention_data_channel.set_finishing();
		usage_data_channel.set_finishing();
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
