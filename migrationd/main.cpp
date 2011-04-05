#ifndef SYSLOG_APP_NAME
#define SYSLOG_APP_NAME "rm-migrationd"
#endif

#ifndef DEFAULT_APP_NAME
#define DEFAULT_APP_NAME "rmmigrationd"
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

#include "migration_data.hpp"
#include "migration_database.hpp"
#include "convert.hpp"
#include "storage.hpp"

#include "../archived/coreapi.hpp"
#include "../archived/policy.hpp"

#include <rml/config.h>
#include <rml/logger.h>
#include <rml/utils.h>
#include <rml/version.h>
#include <rml/siginit.h>

#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/ref.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>

#include <cryptopp/crc.h>
#include <cryptopp/files.h>
#include <cryptopp/filters.h>

#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstddef>

#include <sys/types.h>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <string.h>
#include <signal.h>
#include <time.h>

namespace app {

std::string temp_files_base_directory;
std::string rmfs_base_directory;
std::string hostname;

boost::program_options::variables_map confMap;
volatile bool quit = false;
volatile bool wasHUPed = false;

}

int main(int argc, char* argv[]) try
{
	boost::program_options::variables_map cli_confMap;
	{
		boost::program_options::options_description cli_optsDesc(
					boost::str(boost::format(
							DEFAULT_APP_LONG_NAME" v%1%"
							"\n%2%\n\nOptions"
							) % version_number_string % copyright_string));
		cli_optsDesc.add_options()
				("hostname", boost::program_options::value<std::string>(), "this hostname")
				("help", "this help")
				;
		boost::program_options::store(boost::program_options::parse_command_line(argc, argv, cli_optsDesc), cli_confMap);
		boost::program_options::notify(cli_confMap);

		if (cli_confMap.count("help")) {
			std::cout << cli_optsDesc << "\n";
			return 1;
		}


		if(1 != cli_confMap.count("hostname"))
			throw rml::exception("hostname not given");

		app::hostname = cli_confMap["hostname"].as<std::string>();

		// -------------- config loading ------------------

		boost::program_options::options_description optsDesc;
		optsDesc.add_options()
				("app.run_as_user", boost::program_options::value<std::string>()->default_value(std::string("")), "user to run the daemon as")
				("app.run_as_group", boost::program_options::value<std::string>()->default_value(std::string("")), "group to run the daemon as")
				("app.max_core_size_mega_bytes", boost::program_options::value<unsigned long>()->default_value(300UL), "max core size in mega bytes")
				("app.data_dir", boost::program_options::value<std::string>()->default_value(std::string("/var/lib/archiving")), "data directory")

				("temp_files.base_directory", boost::program_options::value<std::string>()->default_value(std::string("/var/lib/archiving/migration")), "migration temp data directory")
				("rmfs.base_directory", boost::program_options::value<std::string>()->default_value(std::string("/mnt/store01")), "rmfs data directory")

				;
		rml::logger::describe_config(optsDesc);
		app::coreapi::describe_config(optsDesc);

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
				if(-1 == chdir(data_dir.c_str())) {
					THROW_RML_EXCEPTION("Failed changing directory to " << data_dir << " - err:" << strerror(errno));
				}
				else
					LogVerbose("Changed directory to " << data_dir);
			}

			// create / update pid file
			rml::utils::pid_file::create(PID_FILE_PATH);
		}
		catch(const std::exception &e) {
			LogError("Initialization Error - exception: "<<e.what());
			exit(rml::utils::exit_codes::initialization_error);
		}
	}

	app::temp_files_base_directory = app::confMap["temp_files.base_directory"].as<std::string>();
	app::rmfs_base_directory = app::confMap["rmfs.base_directory"].as<std::string>();

	app::migration_data::channel_t channel_data_in("DataIn");
	app::migration_data::channel_t channel_convert("Convert");
	app::migration_data::channel_t channel_copy_to_s3("Copy2S3");
	app::migration_data::channel_t channel_save_retention("SaveRetention");

	app::coreapi coreapi(app::confMap);
	app::policy::cached_finder_t cached_policy_finder(
			boost::bind(&app::policy::finder, boost::ref(coreapi), _1));
	app::storage::s3::endpoint::cached_finder_t cached_endpoint_finder(
			boost::bind(&app::storage::s3::endpoint::finder, boost::ref(coreapi), _1));


	app::database::migration migration_db(app::confMap, channel_data_in, channel_convert, channel_save_retention, cached_policy_finder, cached_endpoint_finder, app::quit);
	app::convert converter(channel_convert, channel_copy_to_s3, app::quit);
	app::migration_storage s3_storage(app::confMap, channel_copy_to_s3, channel_save_retention, app::quit);

	boost::thread_group threads;
	{
		threads.create_thread(boost::bind(&app::database::migration::find_migratables, boost::ref(migration_db)));

		threads.create_thread(boost::bind(&app::convert::run, boost::ref(converter)));
		threads.create_thread(boost::bind(&app::convert::run, boost::ref(converter)));

		threads.create_thread(boost::bind(&app::migration_storage::run, boost::ref(s3_storage)));
		threads.create_thread(boost::bind(&app::migration_storage::run, boost::ref(s3_storage)));

		threads.create_thread(boost::bind(&app::database::migration::update_status, boost::ref(migration_db)));

		threads.create_thread(boost::bind(&app::database::migration::flush_retention, boost::ref(migration_db)));
	}

	while(!app::quit) {
		LogVerbose(((time(0)%2) ? "tik": "tok"));
		boost::this_thread::sleep(boost::posix_time::seconds(1));
	}

	channel_data_in.set_finishing();
	channel_convert.set_finishing();
	channel_copy_to_s3.set_finishing();
	channel_save_retention.set_finishing();

	threads.join_all();

	exit(rml::utils::exit_codes::normal);
}
catch(const std::exception& e) {
	LogError("Exception: "<<e.what());
	exit(rml::utils::exit_codes::failure);
}
catch(...) {
	LogError("Unexpected exception");
	exit(rml::utils::exit_codes::failure);
}
