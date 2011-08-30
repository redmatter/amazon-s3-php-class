/*
 * distiller.cpp
 *
 *  Created on: Apr 28, 2010
 *      Author: dkorah
 */

#include "archived_client.hpp"
#include "cdr_inspector.hpp"
#include "directory_loader.hpp"

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

extern "C"
{
	extern int version_number[3];
	extern const char *version_number_string;
	extern const char *version_date;
	extern const char *build_host;
	extern const char *copyright_string;
}

namespace app {

std::string temp_files_base_directory;
std::string hostname;

boost::program_options::variables_map confMap;
volatile bool quit = false;
volatile bool wasHUPed = false;

}

int main(int argc, char* argv[]) try
{
	bool initialized = false;

	// default initialization to catch any initialization error
	rml::logger::configure();

	boost::program_options::variables_map cli_confMap;
	{
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
	}

	{
		boost::program_options::options_description optsDesc(boost::str( boost::format(DEFAULT_APP_LONG_NAME" v%1%\n%2%\noptions") % version_number_string % copyright_string ));
		optsDesc.add_options()
				("app.data_dir", boost::program_options::value<std::string>()->default_value(std::string("/var/lib/archiving")), "data directory")
				("cdr.directory", boost::program_options::value<std::string>()->default_value(std::string("/opt/freeswitch/log/xml_cdr")), "cdr directory")
				("app.service_threads",  boost::program_options::value<unsigned int>()->default_value(4), "CollectD service threads")
				("app.tik_tok", boost::program_options::value<std::string>()->default_value(std::string("false")), "enable tik tok")
				;

		rml::daemon::describe_config(optsDesc);
		rml::logger::describe_config(optsDesc);

		app::cdr_inspector::describe_config(optsDesc);
		app::archived_client::describe_config(optsDesc);
		app::directory_loader::describe_config(optsDesc);

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
			LogError("Initialization Error - exception: "<<e.what());
			exit(rml::utils::exit_codes::initialization_error);
		}
	}

	try {

		const std::string cdr_directory( app::confMap["cdr.directory"].as<std::string>() );

		archiving::archivable_busy_set_t in_channel_busy_set;
		archiving::archivable_channel_t in_channel(
				boost::bind(&archiving::archivable_busy_set_t::add, boost::ref(in_channel_busy_set), _1),
				boost::bind(&archiving::archivable_busy_set_t::is_busied, boost::ref(in_channel_busy_set), _1),
				0, "IN");
		archiving::archivable_channel_t out_channel("OUT");

		initialized = true;
		LogMessage("Initialisation complete");

		boost::thread_group threads;
		boost::asio::io_service io_service;

		archiving::cdr_dir_monitor cdr_dir_monitor(in_channel, app::hostname);
		archiving::dir_monitor dir_monitor(io_service);
		dir_monitor.add_directory(cdr_directory, archiving::dir_monitor::event::created | archiving::dir_monitor::event::moved_to | archiving::dir_monitor::event::closed);
		dir_monitor.async_monitor(cdr_dir_monitor);

		app::directory_loader::loader_fn directory_loader_fn(
				boost::bind(&archiving::cdr_dir_monitor::add_path, boost::ref(cdr_dir_monitor), _1));
		app::directory_loader directory_loader(cdr_directory, app::confMap, directory_loader_fn, app::quit);
		threads.create_thread(boost::bind(&app::directory_loader::run, boost::ref(directory_loader)));

		app::cdr_inspector cdr_inspector(in_channel, out_channel, app::confMap, app::hostname);
		for(unsigned int i = app::confMap["cdr_handler.number_of_threads"].as<unsigned int>(); i > 0; --i)
			threads.create_thread(boost::bind(&app::cdr_inspector::run, boost::ref(cdr_inspector)));

	    app::archived_client archived_client(
	    		io_service,
	    		out_channel,
	    		app::confMap["archived_client.server_host"].as<std::string>(),
	    		app::confMap["archived_client.server_port"].as<std::string>());

		{
			unsigned int n_threads = ( 1 + std::max<unsigned int>( app::confMap["app.service_threads"].as<unsigned int>(), 4 ));
			LogMessage("Starting "<<(n_threads-1)<<"+1 service threads");
			while(0 < --n_threads)
				threads.create_thread(boost::bind(&boost::asio::io_service::run, boost::ref(io_service)));
		}

		while(!app::quit) {
			if(app::confMap["app.tik_tok"].as<std::string>() == "true")
				LogDebug(((time(0)%2) ? "tik": "tok"));

			boost::this_thread::sleep(boost::posix_time::seconds(1));
		}

		in_channel.set_finishing();
		out_channel.set_finishing();

		io_service.stop();

		threads.join_all();


		LogMessage("Program terminated normally");

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
