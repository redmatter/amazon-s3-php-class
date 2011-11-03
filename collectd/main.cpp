/*
 * distiller.cpp
 *
 *  Created on: Apr 28, 2010
 *      Author: dkorah
 */

#include "../common/archived_client.hpp"
#include "cdr_inspector.hpp"
#include "directory_loader.hpp"
#include "../common/database.hpp"

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

class archivable_dedup
{
	const archiving::archivable_busy_set_t& busy_set_;
	database::internal& internal_db_;

public:
	archivable_dedup(const archiving::archivable_busy_set_t& busy_set, database::internal& internal_db) : busy_set_(busy_set), internal_db_(internal_db)
	{
	}

	inline bool is_archivable_loaded(archiving::archivable::pointer p_arc) const
	{
		const bool& is_busied(busy_set_.is_busied(p_arc));
		LogDebug(p_arc<<" is_archivable_loaded [busied]: "<<is_busied);

		if(is_busied)
			return true;

		// check database as well
		const bool& is_queued(internal_db_.is_queued(p_arc));
		LogDebug(p_arc<<" is_archivable_loaded [queued]: "<<is_queued);

		return is_queued;
	}

	inline bool is_file_loaded(const archiving::archivable::file_type::file_type_t& type, const std::string& file_path) const
	{
		return is_loaded(type, std::string(file_path, file_path.length() - (36/*uuid*/+4/*.cdr*/+4/*.xml*/), 36));
	}

	inline bool is_loaded(const archiving::archivable::file_type::file_type_t& type, const std::string& uuid) const
	{
		return is_archivable_loaded(archiving::archivable::pointer(new archiving::archivable(uuid, type)));
	}

	inline archiving::archivable::pointer get_loaded_instance(const archiving::archivable::pointer& p_lookup) const
	{
		return busy_set_.get_item(p_lookup);
	}
};

bool unlink_path(const std::string& path)
{
	if(0 != chmod(path.c_str(), 0666)) {
		if(errno == ENOENT) {
			LogMessage("File "<<path<<" doesn't exist; may be already unlinked");
			return true;
		}

		LogWarning("chmod failed "<<path<<" - err:" << strerror(errno));
	}

	if(0 != unlink(path.c_str()) && (errno != ENOENT)) {
		LogWarning("unlink failed "<<path<<" - err:" << strerror(errno));
	}
	else {
		LogVerbose(path << " unlinked");
		return true;
	}

	return false;
}

void purge(database::internal& internal_db, volatile bool& quit, const std::string& pbx_hostname, const unsigned int done_items_max_age_minutes, const unsigned int max_age_days, const unsigned int limit, const unsigned int frequency_minutes)
{
	int ticks = 1;

	LogDebug("Purge thread starting... frequency_minutes:"<<frequency_minutes);

	while(!quit) {

		boost::this_thread::sleep(boost::posix_time::seconds(1));
		if(ticks++ % (frequency_minutes * 60))
			continue;

		try {
			const database::internal::purgedef::pointer_vector& purgeables( internal_db.purge_find(pbx_hostname, done_items_max_age_minutes, max_age_days, limit) );

			if(!purgeables.empty()) {
				LogMessage("Purging done queue items...");
				for(database::internal::purgedef::pointer_vector::const_iterator itr = purgeables.begin();
						itr != purgeables.end(); ++itr) {
					if(!(*itr)->modified_path.empty())
						unlink_path((*itr)->modified_path);

					unlink_path((*itr)->path);
				}

				if(!internal_db.purge(purgeables))
					LogWarning("Purge DB failed; ignoring ...");
			}
		}
		catch(const std::exception& e) {
			LogWarning("Exception: "<<e.what());
		}
	}

	LogMessage("Purge closing");
}

std::string temp_files_base_directory;
std::string hostname;

boost::program_options::variables_map confMap;
volatile bool quit = false;
volatile bool wasHUPed = false;

}

namespace {

bool more_to_load_from_db = false;
inline bool continue_loading_dir() {
	return !more_to_load_from_db && app::archived_client::get_is_archived_ok();
}

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
				("purge.done_items_max_age_minutes",  boost::program_options::value<unsigned int>()->default_value(20), "Maximum age in minutes for done items")
				("purge.max_age_days",  boost::program_options::value<unsigned int>()->default_value(1), "Maximum age")
				("purge.batch_size",  boost::program_options::value<unsigned int>()->default_value(500), "Purge batch size")
				("purge.frequency_minutes",  boost::program_options::value<unsigned int>()->default_value(1), "Purge frequency")
				;

		rml::daemon::describe_config(optsDesc);
		rml::logger::describe_config(optsDesc);

		app::cdr_inspector::describe_config(optsDesc);
		app::archived_client::describe_config(optsDesc);
		app::directory_loader::describe_config(optsDesc);
		app::database::internal::describe_config(optsDesc);

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

		archiving::archivable_busy_set_t archivable_busy_set;
		archiving::archivable_channel_t in_channel(
				boost::bind(&archiving::archivable_busy_set_t::add, boost::ref(archivable_busy_set), _1),
				boost::bind(&archiving::archivable_busy_set_t::is_busied, boost::ref(archivable_busy_set), _1),
				0, "IN");
		archiving::archivable_channel_t out_channel("OUT");

		app::database::internal internal_db(app::confMap, "CollectDQueue");

		initialized = true;
		LogMessage("Initialisation complete");

		boost::thread_group threads;
		boost::asio::io_service io_service;

		archiving::cdr_dir_monitor cdr_dir_monitor(in_channel, app::hostname);
		archiving::dir_monitor dir_monitor(io_service);
		dir_monitor.add_directory(cdr_directory, archiving::dir_monitor::event::created | archiving::dir_monitor::event::moved_to | archiving::dir_monitor::event::closed);
		dir_monitor.async_monitor(cdr_dir_monitor);

		app::directory_loader::continue_loading_fn continue_loading_fn(boost::bind(continue_loading_dir));

		app::directory_loader::loader_fn directory_loader_fn(
				boost::bind(&archiving::cdr_dir_monitor::add_path, boost::ref(cdr_dir_monitor), _1));

		app::archivable_dedup archivable_dedup(archivable_busy_set, internal_db);
		app::directory_loader::is_loaded_fn archivable_is_loaded_fn(
				boost::bind(&app::archivable_dedup::is_file_loaded, boost::ref(archivable_dedup), archiving::archivable::file_type::cdr, _1));

		boost::function<archiving::archivable::pointer(const archiving::archivable::pointer&)> fn_get_loaded_instance(
				boost::bind(&app::archivable_dedup::get_loaded_instance, boost::ref(archivable_dedup), _1));

		// initialise the internal static variables before we load anything;
		internal_db.find_archivables(app::hostname, fn_get_loaded_instance, in_channel, more_to_load_from_db);

		app::directory_loader directory_loader(cdr_directory, app::confMap, directory_loader_fn, archivable_is_loaded_fn, continue_loading_fn, app::quit);
		threads.create_thread(boost::bind(&app::directory_loader::run, boost::ref(directory_loader)));

		app::cdr_inspector cdr_inspector(in_channel, out_channel, archivable_busy_set, internal_db, app::confMap, app::hostname);
		for(unsigned int i = app::confMap["cdr_handler.number_of_threads"].as<unsigned int>(); i > 0; --i)
			threads.create_thread(boost::bind(&app::cdr_inspector::run, boost::ref(cdr_inspector)));

		const unsigned int n_threads( 2 + std::max<unsigned int>( app::confMap["app.service_threads"].as<unsigned int>(), 1 ));
		LogMessage("Starting "<<(n_threads-1)<<"+1 service threads");

		std::vector< app::archived_client::pointer > archived_client(n_threads - 2);
		for(unsigned int i = 0; i < (n_threads - 2); i++) {
			archived_client.push_back(app::archived_client::pointer(new app::archived_client(
		    		io_service, out_channel, archivable_busy_set,
		    		app::confMap["archived_client.server_host"].as<std::string>(),
		    		app::confMap["archived_client.server_port"].as<std::string>(),
		    		internal_db, app::quit)));

			boost::this_thread::sleep(boost::posix_time::milliseconds(500));
		}

		for(unsigned int i = 0; i < n_threads; i++)
			threads.create_thread(boost::bind(&boost::asio::io_service::run, boost::ref(io_service)));

		threads.create_thread(boost::bind(app::purge,
				boost::ref(internal_db), boost::ref(app::quit), boost::ref(app::hostname),
				app::confMap["purge.done_items_max_age_minutes"].as<unsigned int>(),
				app::confMap["purge.max_age_days"].as<unsigned int>(),
				app::confMap["purge.batch_size"].as<unsigned int>(),
				app::confMap["purge.frequency_minutes"].as<unsigned int>()));

		int ticks = 1;
		while(!app::quit) {
			if(app::confMap["app.tik_tok"].as<std::string>() == "true")
				LogDebug(((time(0)%2) ? "tik": "tok"));

			if(more_to_load_from_db && !(ticks % 10)) {
				if(app::archived_client::get_is_archived_ok())
					internal_db.find_archivables(app::hostname, fn_get_loaded_instance, in_channel, more_to_load_from_db);
				else
					LogMessage("Database loading paused; waiting for ArchiveDs to come back online");
			}

			if(!(ticks % 60))
				LogMessage("[statistics] IN:"<<in_channel.count()<<" OUT:"<<out_channel.count()<<" DBLoading:"<<RML_YES_NO(more_to_load_from_db));

			boost::this_thread::sleep(boost::posix_time::seconds(1));

			ticks++;
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
