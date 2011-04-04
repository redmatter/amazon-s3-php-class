#include "globals.h"
#include "put_session.hpp"
#include "put_server.hpp"
#include "storage.hpp"
#include "storage_operation.hpp"
#include "storage_runner.hpp"

#include "../common/types.hpp"
#include "../common/cdr_dir_monitor.hpp"
#include "../common/dir_monitor.hpp"

#include <rml/logger.h>
#include <rml/utils.h>
#include <rml/thread_adapter.h>
#include <rml/config.h>
#include <rml/siginit.h>

#include <libaws/aws.h>

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

// the configuration map
boost::program_options::variables_map confMap;

// time to quit?
volatile bool quit = false;

// was HUPed
volatile bool wasHUPed = false;

}

int main(int argc, char **argv)
{
	{
		boost::program_options::options_description optsDesc(boost::str( boost::format(DEFAULT_APP_LONG_NAME" v%1%\n%2%\noptions") % version_number_string % copyright_string ));
		optsDesc.add_options()
				("app.run_as_user", boost::program_options::value<std::string>()->default_value(std::string("")), "user to run the daemon as")
				("app.run_as_group", boost::program_options::value<std::string>()->default_value(std::string("")), "group to run the daemon as")
				("app.max_core_size_mega_bytes", boost::program_options::value<unsigned long>()->default_value(300UL), "max core size in mega bytes")
				("app.data_dir", boost::program_options::value<std::string>()->default_value(std::string("/var/lib/archiving")), "data directory")
				("archived.service_threads",  boost::program_options::value<unsigned int>()->default_value(4), "ArchiveD service threads")
				;

		rml::logger::describe_config(optsDesc);

		app::put_session::describe_config(optsDesc);
		app::put_server::describe_config(optsDesc);
		app::storage::s3::backend::describe_config(optsDesc);

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
		archiving::archivable_channel_t put_channel("->PUT");
		archiving::archivable_channel_t to_s3_channel("PUT->S3");

		app::storage::s3::operation_channel_t s3_operation_channel("s3ops");

		boost::thread_group threads;
		boost::asio::io_service io_service;

		app::put_server put_server(app::confMap, put_channel, to_s3_channel, io_service);

		app::storage::s3::storage s3storage(app::confMap);
		app::storage::s3::runner s3runner(s3storage, s3_operation_channel, app::quit);

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

		while(!app::quit) {
			LogVerbose(((time(0)%2) ? "tik": "tok"));
			boost::this_thread::sleep(boost::posix_time::seconds(1));
		}

		put_channel.set_finishing();
		to_s3_channel.set_finishing();

		io_service.stop();

		threads.join_all();

		LogAlways("terminated normally");

		rml::logger::deconfigure();

		exit(rml::utils::exit_codes::normal);
	}
	catch(const std::exception &e) {
		LogCritical( e.what() );
	}

	LogCritical( "terminated with failure" );
	exit(rml::utils::exit_codes::failure);
}
