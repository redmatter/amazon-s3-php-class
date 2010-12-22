/*
 * distiller.cpp
 *
 *  Created on: Apr 28, 2010
 *      Author: dkorah
 */

#include <rml/logger.h>
#include <rml/utils.h>
#include <rml/thread_adapter.h>

#include <sys/types.h>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <string.h>

#include <iostream>
#include <fstream>

#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>

#ifndef SYSLOG_APP_NAME
#define SYSLOG_APP_NAME "rm-collectd"
#endif

#ifndef DEFAULT_APP_NAME
#define DEFAULT_APP_NAME "rmcollectd"
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
	extern int version_number[3];
	extern const char *version_number_string;
	extern const char *version_date;
	extern const char *build_host;
	extern const char *copyright_string;
}

namespace app {

// the configuration map
boost::program_options::variables_map confMap;

// time to quit?
bool quit = false;

// reload time?
bool wasHUPed = false;

}

/* save+quit */
void sigint(int s) {
	LogError("SIGINT: Trying a clean exit");
	app::quit = true;
}

/* quit */
void sighup(int s) {
	LogError("SIGHUPed");
	app::wasHUPed = true;
}

/* kill */
void sigterm(int s) {
	LogError("SIGTERM: Trying a clean exit");
	app::quit = true;
}

/* save */
void sigusr1(int s) {
	LogError("SIGUSR1 Ignored");
}

/* reset all */
void sigusr2(int s) {
	LogError("SIGUSR2 Ignored");
}

/* sigpipe ?! */
void sigpipe(int s) {
	LogError("SIGPIP Ignored");
}

void siginit(void) {
	signal(SIGINT,(__sighandler_t)sigint);
	signal(SIGHUP,(__sighandler_t)sighup);
	signal(SIGUSR1,(__sighandler_t)sigusr1);
	signal(SIGUSR2,(__sighandler_t)sigusr2);
	signal(SIGPIPE,(__sighandler_t)sigpipe);
	signal(SIGTERM,(__sighandler_t)sigterm);
}

int main(int argc, char **argv) {

	{
		namespace bopts = boost::program_options;

		bopts::options_description optsDesc(boost::str( boost::format(DEFAULT_APP_LONG_NAME" v%1%\n%2%\noptions") % version_number_string % copyright_string ));
		optsDesc.add_options()

				("app.run_as_user", bopts::value<std::string>()->default_value(std::string("")), "user to run the daemon as")
				("app.run_as_group", bopts::value<std::string>()->default_value(std::string("")), "group to run the daemon as")
				("app.max_core_size_mega_bytes", bopts::value<unsigned long>()->default_value(300UL), "max core size in mega bytes")

				("app.config_include_dir", bopts::value<std::string>()->default_value(std::string(CONFIG_FILE_PATH".d")), "config include directory")

				("log.file_path", bopts::value<std::string>()->default_value(std::string(DEFAULT_LOG_FILE_PATH)), "logfile path")
				("log.level", bopts::value<int>()->default_value(4), "logging level")
				("log.stderr_level", bopts::value<int>()->default_value(2), "logging level for stderr")

				;
		std::ifstream ifs_config(CONFIG_FILE_PATH);
		if(!ifs_config) {
			std::cerr << "Unable to open config: "CONFIG_FILE_PATH"\n";
			exit(rml::utils::exit::codes::config_file_missing);
		}

		try {
			bopts::store(bopts::parse_config_file(ifs_config, optsDesc, true), app::confMap);
			bopts::notify(app::confMap);
		}
		catch(const std::exception &e) {
			LogError("Error loading configuration - exception: "<<e.what());
			exit(rml::utils::exit::codes::config_file_invalid);
		}
	}

	rml::logger::configure(app::confMap, SYSLOG_APP_NAME);

	LogAlways(DEFAULT_APP_LONG_NAME" v"<<version_number_string<<' '<<copyright_string);
	LogAlways("built on "<<version_date<<' '<<build_host);

	try {
		siginit();

		// set core limit
		{
			struct rlimit rlim;
			if(getrlimit(RLIMIT_CORE, &rlim) == 0)
			{
				unsigned long core_limit = app::confMap["cdrpostd.max_core_size_mega_bytes"].as<unsigned long>();
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
			std::string runas_user = app::confMap["cdrpostd.run_as_user"].as<std::string>();
			std::string runas_group = app::confMap["cdrpostd.run_as_group"].as<std::string>();

			std::string home_dir = "/var/lib/archiving";

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
					home_dir = pw->pw_dir;

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
			if(-1 == chdir(home_dir.c_str()))
				LogError("Failed changing directory to " << home_dir << " - err:" << strerror(errno));
			else
				LogVerbose("Changed directory to " << home_dir);
		}

		rml::utils::pid_file::create(PID_FILE_PATH);

/*
		app::cdr_dir = app::confMap["cdrpostd.freeswitch_cdr_log_dir"].as<std::string>();
		LogVerbose("CDRs will be read from " << app::cdr_dir);

		app::cdr_channel theChannel;

		boost::thread_group threads;

	    boost::asio::io_service io_service;
	    app::dir_monitor dir_monitor(io_service);

		dir_monitor.add_directory(app::cdr_dir, app::dir_monitor::event::created | app::dir_monitor::event::closed);

		cdr_arrival_notification cdr_arrival_notification_(app::cdr_dir, theChannel);
		dir_monitor.async_monitor(cdr_arrival_notification_);

        {
            char hostname_buf[256];
            {
				if(gethostname(hostname_buf, 256)) {
					LogError("gethostname failed - err:"<<strerror(errno));
					throw rml::exception("gethostname failed");
				}

				char* dot = strchrnul(hostname_buf, '.');
				*dot = '\0';
            }

            app::cdr_poster::init(
            		app::confMap["cdrpostd.disable_100_continue"].as<std::string>() == "true"
            		,app::confMap["cdrpostd.post_timeout_seconds"].as<unsigned int>()
            		,app::confMap["cdrpostd.post_url"].as<std::string>()
            		,hostname_buf
            		,app::cdr_dir);
        }

		boost::this_thread::sleep(boost::posix_time::seconds(1));

		const unsigned int number_of_post_threads = app::confMap["cdrpostd.number_of_poster_threads"].as<unsigned int>();
		std::vector<app::cdr_poster*> posters(number_of_post_threads);
		std::vector<boost::thread*> thread_posters(number_of_post_threads);

		for(unsigned int i = number_of_post_threads; i > 0; --i) {
			app::cdr_poster* p = new app::cdr_poster(theChannel);
			boost::thread* t = new boost::thread(rml::thread_adapter(&app::cdr_poster::do_thread, p));
			posters.push_back(p);
			thread_posters.push_back(t);
			threads.add_thread(t);
		}

		LogMessage(number_of_post_threads << " poster(s) started");

		app::directory_loader *dir_loader = new app::directory_loader(theChannel, app::cdr_dir);
		boost::thread* dir_loader_thread = new boost::thread(rml::thread_adapter(&(dir_loader->do_thread), dir_loader));
		threads.add_thread(dir_loader_thread);

		while(!app::quit) {
			boost::this_thread::sleep(boost::posix_time::seconds(1));
			io_service.poll();
		}

		io_service.stop();
		theChannel.set_finishing();
		threads.join_all();

		for(unsigned int i = number_of_post_threads; i > 0; --i) {
			threads.remove_thread(thread_posters[i]);
			delete thread_posters[i];
			delete posters[i];
		}

		threads.remove_thread(dir_loader_thread);
		delete dir_loader_thread;
		delete dir_loader;
*/
		LogAlways("terminated normally");

		rml::logger::deconfigure();

		exit(rml::utils::exit::codes::normal);
	}
	catch(const std::exception &e) {
		LogCritical( e.what() );
	}

	LogCritical( "terminated with failure" );
	exit(rml::utils::exit::codes::failure);
}
