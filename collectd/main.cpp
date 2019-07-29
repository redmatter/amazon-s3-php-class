//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 28 April 2010.
//

#include "cdr_inspector.hpp"
#include "directory_loader.hpp"
#include "cdr_dir_monitor.hpp"
#include "net/archived_client.hpp"
#include "put_group_cache.hpp"

#include "archivable/queue.hpp"
#include "../common/collectd/callbacks.hpp"
#include "../common/host_ip_change_detector.hpp"

#include "database_sqlite.hpp"
#include <rml/sqlite/status.hpp>

#include "../common/types.hpp"
#include "../common/inotify_monitor.hpp"
#include "../common/hostname.hpp"
#include "../common/net/archived/connection_manager.hpp"

#include <rml/logger.h>
#include <rml/utils.h>
#include <rml/thread_adapter.h>
#include <rml/config.h>
#include <rml/siginit.h>
#include <rml/hapi.hpp>
#include <rml/daemon.h>
#include <rml/version.h>
#include <rml/utils/saver_list.hpp>

#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/ref.hpp>
#include <boost/bind.hpp>
#include <boost/bind/protect.hpp>
#include <boost/chrono/chrono_io.hpp>

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

namespace app {

class archivable_dedup
{
	boost::shared_ptr< collectd::archivable::catalogue > busy_set_;
	database::internal& internal_db_;

public:
	archivable_dedup(const boost::shared_ptr<collectd::archivable::catalogue> &busy_set,
					 database::internal &internal_db) :
			busy_set_(busy_set), internal_db_(internal_db)
	{
	}

	inline bool is_archivable_loaded(archiving::archivable::pointer p_arc) const
	{
		const bool& is_busied(busy_set_->is_present(p_arc));
		LogDebug(p_arc<<" is_archivable_loaded [busied]: "<<is_busied);

		if(is_busied)
			return true;

		// check database as well
		const bool& is_queued(internal_db_.is_queued(p_arc));
		LogDebug(p_arc<<" is_archivable_loaded [queued]: "<<is_queued);

		return is_queued;
	}

	inline bool is_file_loaded(const archiving::file_type::file_type_t& type, const size_t& rev_path_uuid_idx, const std::string& file_path) const
	{
		return is_loaded(type, std::string(file_path, file_path.length() - rev_path_uuid_idx, 36));
	}

	inline bool is_file_loaded(const archiving::cdr_dir_monitor::filename_decoder_t& filename_decoder, const std::string& file_path) const
	{
		size_t rev_path_uuid_idx;
		archiving::file_type::file_type_t type;
		std::string data_format;
		if (filename_decoder(file_path, rev_path_uuid_idx, type, data_format))
			return is_file_loaded(type, rev_path_uuid_idx, file_path);

		LogWarning("Unknown filename format found; "<<file_path<<"; ignored");
		return true; // true to prevent from loading
	}

	inline bool is_loaded(const archiving::file_type::file_type_t& type, const std::string& uuid) const
	{
		return is_archivable_loaded(boost::make_shared<archiving::archivable>(uuid, type));
	}

	inline archiving::archivable::pointer get_loaded_instance(const archiving::archivable::pointer& p_lookup) const
	{
		boost::system::error_condition ec;
		const archiving::archivable::pointer& ret(busy_set_->get_item(p_lookup, ec));
		BOOST_ASSERT((ec && !ret) || (!ec && ret));
		return ret;
	}
};

bool unlink_path(const std::string& path)
{
	if(0 != chmod(path.c_str(), 0666)) {
		if(errno == ENOENT) {
			LogVerbose("File "<<path<<" doesn't exist; may be already unlinked");
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

void purge(database::internal& internal_db, volatile bool& quit, const std::string& pbx_hostname,
		const unsigned int done_items_max_age_minutes, const unsigned int max_age_days, const unsigned int limit,
		const unsigned int frequency_minutes)
{
	int ticks = 1;

	LogDebug("Purge thread starting... frequency_minutes:"<<frequency_minutes);

	while(!quit) {

		boost::this_thread::sleep_for(boost::chrono::seconds(1));
		if(ticks++ % (frequency_minutes * 60))
			continue;

		try {
			const database::internal::purgedef::pointer_vector& purgeables(
					internal_db.purge_find(pbx_hostname, done_items_max_age_minutes, max_age_days, limit));

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

const time_t startup_time(time(0));

} // namespace app

namespace {

bool more_to_load_from_db = false;
inline float continue_loading_dir(const boost::function<float(boost::system::error_code&)>& get_archived_availability) {
	if (app::quit) {
		return 0.0;
	}

	boost::system::error_code ec;
	float availability = get_archived_availability(ec);
	if (ec) {
		// if we can't get the availability, we still need to keep loading at a slow pace
		availability = 0.1;
	}

	return std::min<float>(
		std::max<float>(availability, 0.25), 1.0);
}

/**
 * Identify PCap type from file-path and set uuid-offset, file-type and data-format
 * Assumes PCAP filename format is [uuid].pcap OR [uuid].sip.pcap OR [uuid].rtp.pcap
 *
 * @param file_path
 * @param rev_path_uuid_idx
 * @param file_type
 * @param data_format
 *
 * @return true if successful; false means that we could not decode the file-name
 */
bool identify_pcap_type(
		const std::string& file_path,
		size_t& rev_path_uuid_idx, archiving::file_type::file_type_t& file_type, std::string& data_format)
{
	const size_t& dot_pcap(file_path.rfind(".pcap"));
	if(dot_pcap != std::string::npos && dot_pcap == (file_path.length() - 5))
	{
		const size_t& first_dot(file_path.find('.'));
		if(first_dot == dot_pcap)
		{
			rev_path_uuid_idx = size_t(36/*uuid*/+5/*.pcap*/);
			file_type = archiving::file_type::pcap;
			data_format = "pcap";

			return true;
		}
		else if(first_dot == dot_pcap - 4)
		{
			rev_path_uuid_idx = size_t(36/*uuid*/+4/*.(sip|rtp)*/+5/*.pcap*/);
			data_format = "pcap";
			file_type = (0 == strncmp(&file_path[0] + first_dot + 1, "sip", 3))?
					archiving::file_type::pcap_sip_only :
					archiving::file_type::pcap;

			return true;
		}
		else
			LogWarning("Unrecognised PCap file: "<<file_path<<"; ignored");
	}

	return false;
}

} // namespace

/**
 * The main!
 *
 * @param argc
 * @param argv
 * @return
 */
int main(int argc, char* argv[]) try
{
	bool initialized = false;

	// default initialization to catch any initialization error
	rml::logger::configure();
	rml::hapi::host_cache::initialise();

	boost::program_options::variables_map cli_confMap;
	{
		boost::program_options::options_description cli_optsDesc(
					boost::str(boost::format(
							DEFAULT_APP_LONG_NAME" v%1%"
							"\n%2%\n\nOptions"
							) % version_number_string % copyright_string));
		cli_optsDesc.add_options()
				("hostname", boost::program_options::value<std::string>(), "this hostname")
				("conf", boost::program_options::value<std::string>()->default_value(CONFIG_FILE_PATH), "config file")
				("help-conf", "conf file help")
				("help", "this help")
				("version", "shows version information")
				;
		boost::program_options::store(boost::program_options::parse_command_line(argc, argv, cli_optsDesc), cli_confMap);
		boost::program_options::notify(cli_confMap);

		if (cli_confMap.count("help")) {
			std::cout << cli_optsDesc << "\n";
			exit(rml::utils::exit_codes::normal);
		}
	}

	if(cli_confMap.count("version")) {
		std::cout << boost::str( boost::format(DEFAULT_APP_LONG_NAME" v%1%\n%2%\n") % version_number_string % copyright_string );
		exit(rml::utils::exit_codes::normal);
	}

	{
		boost::program_options::options_description optsDesc(
				boost::str( boost::format(DEFAULT_APP_LONG_NAME" v%1%\n%2%\noptions") %
						version_number_string %
						copyright_string ));
		optsDesc.add_options()
				("app.data_dir", boost::program_options::value<std::string>()
						->default_value(std::string("/var/lib/archiving")), "data directory")
				("cdr.directory", boost::program_options::value<std::string>()
						->default_value(std::string("/opt/freeswitch/log/xml_cdr")), "cdr directory")
				("pcap.enabled", boost::program_options::value<bool>()
						->default_value(false), "pcaps handling enabled")
				("pcap.directory", boost::program_options::value<std::string>()
						->default_value(std::string("/var/log/freeswitch/pcap")), "pcaps finished directory")
				("chunk.enabled", boost::program_options::value<bool>()
						->default_value(false), "chunk handling enabled")
				("chunk.directory", boost::program_options::value<std::string>()
						->default_value(std::string("/opt/freeswitch/log/chunk")), "chunked recordings directory")
				("info.enabled", boost::program_options::value<bool>()
						->default_value(false), "info file handling enabled")
				("info.directory", boost::program_options::value<std::string>()
						->default_value(std::string("/opt/freeswitch/log/info")), "info files directory")
				("app.tik_tok", boost::program_options::value<std::string>()
						->default_value(std::string("false")), "enable tik tok")
				("purge.done_items_max_age_minutes",  boost::program_options::value<unsigned int>()
						->default_value(20), "Maximum age in minutes for done items")
				("purge.max_age_days",  boost::program_options::value<unsigned int>()
						->default_value(1), "Maximum age")
				("purge.batch_size",  boost::program_options::value<unsigned int>()
						->default_value(500), "Purge batch size")
				("purge.frequency_minutes",  boost::program_options::value<unsigned int>()
						->default_value(1), "Purge frequency")
				;

		rml::daemon::describe_config(optsDesc);
		rml::logger::describe_config(optsDesc);

		app::cdr_inspector::describe_config(optsDesc);
		common::net::archived::connection_manager::describe_config(optsDesc);
		app::directory_loader::describe_config(optsDesc);
		app::database::internal::describe_config(optsDesc);

		if(cli_confMap.count("help-conf")) {
			std::cout << optsDesc << "\n";
			return 1;
		}

		if(cli_confMap.count("hostname")) {
			app::hostname = cli_confMap["hostname"].as<std::string>();
		} else {
			app::hostname = archiving::common::get_hostname();
		}

		try {
			rml::config::parse(cli_confMap["conf"].as<std::string>(), optsDesc, app::confMap);

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

		const std::string cdr_directory( app::confMap["cdr.directory"].as<std::string>() );
		const std::string pcap_directory( app::confMap["pcap.directory"].as<std::string>() );
		const std::string chunk_directory( app::confMap["chunk.directory"].as<std::string>() );
		const std::string info_directory( app::confMap["info.directory"].as<std::string>() );

		boost::shared_ptr< collectd::archivable::catalogue > archivable_catalogue =
				boost::make_shared<collectd::archivable::catalogue>();
		const common::collectd::fn_remove_from_catalogue_type fn_remove_from_catalogue(
				boost::bind(&collectd::archivable::catalogue::remove, archivable_catalogue, _1));
		const common::collectd::fn_is_in_catalogue_type fn_is_in_catalogue(
				boost::bind(&collectd::archivable::catalogue::is_present, archivable_catalogue, _1));

		boost::shared_ptr<collectd::archivable::queue>
				in_queue = boost::make_shared<collectd::archivable::queue>("IN", archivable_catalogue),
				out_queue = boost::make_shared<collectd::archivable::queue>("OUT", archivable_catalogue);
		const common::collectd::fn_produce_type fn_in_queue_produce(
				boost::bind(
						&collectd::archivable::queue::produce, in_queue, _1, boost::chrono::milliseconds::zero(), _2));
		const common::collectd::fn_produce_type fn_out_queue_produce(
				boost::bind(
						&collectd::archivable::queue::produce, out_queue, _1, boost::chrono::milliseconds::zero(), _2));

		const common::collectd::fn_consume_type fn_in_queue_consume(
				boost::bind(&collectd::archivable::queue::consume, in_queue, boost::chrono::milliseconds::zero(), _1));
		const common::collectd::fn_consume_with_timeout_finishing_callback_type fn_out_queue_consume(
				boost::bind(&collectd::archivable::queue::consume, out_queue, _2, _1, _3, _4));

		app::database::internal internal_db(app::confMap, "CollectDQueue");

		typedef rml::utils::saver_list<boost::shared_ptr<app::put_group> > put_group_saver_list_type;

		put_group_saver_list_type::save_range_callback_type put_group_saver_list_save_range_callback(
			boost::bind(
				&app::database::internal::put_group_save_bulk<put_group_saver_list_type::list_citr_type>, 
				boost::ref(internal_db), _1, _2));

		put_group_saver_list_type put_group_saver_list(
			100, boost::chrono::seconds(30), boost::protect(put_group_saver_list_save_range_callback));

		app::put_group_cache::save_callback_type put_group_save_callback(
			boost::bind(&put_group_saver_list_type::add, boost::ref(put_group_saver_list), _1));
		app::put_group_cache::load_callback_type put_group_load_callback(
			boost::bind(&app::database::internal::put_group_load, boost::ref(internal_db), _1));
		
		app::put_group_cache put_group_cache(put_group_load_callback, put_group_save_callback);

		const common::collectd::fn_save_to_database_type fn_save_to_database(
				boost::bind(&app::database::internal::save, boost::ref(internal_db), _1));

		const common::collectd::fn_lookup_put_group_archived_ip_type fn_lookup_put_group_archived_ip(
				boost::bind(&app::put_group_cache::get, boost::ref(put_group_cache), _1));

		const common::collectd::fn_set_put_group_archived_ip_type fn_set_put_group_archived_ip(
				boost::bind(&app::put_group_cache::put, boost::ref(put_group_cache), _1, _2));

		const common::collectd::fn_add_put_group_type fn_add_put_group(
				boost::bind(&app::put_group_cache::put, boost::ref(put_group_cache), _1, 0)); // always add with ip=0

		const common::collectd::fn_remove_put_group_type fn_remove_put_group(
				boost::bind(&app::put_group_cache::remove, boost::ref(put_group_cache), _1));

		initialized = true;
		LogMessage("Initialisation complete");

		boost::thread_group threads, cdr_inspector_threads, dir_loader_threads;
		boost::asio::io_service io_service;
		boost::shared_ptr<boost::asio::io_service::work> p_work = boost::make_shared<boost::asio::io_service::work>(boost::ref(io_service));

		const std::string archived_host(app::confMap["archived_client.server_host"].as<std::string>());
		const std::string archived_port(app::confMap["archived_client.server_port"].as<std::string>());

		boost::shared_ptr<common::net::archived::connection_manager> conn_mgr =
				boost::make_shared<common::net::archived::connection_manager>(
						boost::ref(io_service), archived_host, archived_port,
						0, app::confMap["archived_connection_pool.max_connections"].as<std::size_t>());

		boost::function<float()> continue_loading_dir_callback = boost::bind(
				continue_loading_dir, boost::protect(boost::bind(
						&common::net::archived::connection_manager::get_pool_level, conn_mgr, _1)));

		boost::shared_ptr<collectd::net::archived_client> archived_client =
				boost::shared_ptr<collectd::net::archived_client>(new collectd::net::archived_client(
						conn_mgr, fn_out_queue_produce, fn_out_queue_consume,
						fn_remove_from_catalogue, fn_save_to_database, common::collectd::fn_on_successful_put_type(),
						fn_lookup_put_group_archived_ip, fn_set_put_group_archived_ip, fn_remove_put_group));

		archiving::cdr_dir_monitor cdr_dir_monitor(boost::bind(fn_in_queue_produce, _1, true), app::hostname);

		// setup the inotify monitor
		boost::shared_ptr<app::inotify::monitor> inotify_monitor = boost::make_shared<app::inotify::monitor>(boost::ref(io_service));

		// monitor freeswitch's xml_cdr folder for new CDRs
		// Assumes CDR filename format is [uuid].cdr.xml.
		inotify_monitor->add_directory(
				cdr_directory,
				app::inotify::event::created | app::inotify::event::moved_to | app::inotify::event::closed,
				boost::bind(&archiving::cdr_dir_monitor::handle_event, boost::ref(cdr_dir_monitor), _1, _2,
						size_t(36/*uuid*/+4/*.cdr*/+4/*.xml*/), archiving::file_type::cdr, "xml"));

		// monitor the PCap path for any new arrivals
		// Assumes PCAP filename format is [uuid].pcap OR [uuid].sip.pcap OR [uuid].rtp.pcap
		if(app::confMap["pcap.enabled"].as<bool>())
			inotify_monitor->add_directory(
					pcap_directory,
					app::inotify::event::created | app::inotify::event::moved_to | app::inotify::event::closed,
					boost::bind(&archiving::cdr_dir_monitor::handle_event, boost::ref(cdr_dir_monitor), _1, _2, identify_pcap_type));

		// monitor folder for recording chunks
		// Assumes chunked recording filename format ends with [uuid].wav
		if(app::confMap["chunk.enabled"].as<bool>())
			inotify_monitor->add_directory(
					chunk_directory,
					app::inotify::event::created | app::inotify::event::moved_to | app::inotify::event::closed,
					boost::bind(&archiving::cdr_dir_monitor::handle_event, boost::ref(cdr_dir_monitor), _1, _2,
							size_t(36/*uuid*/+4/*.wav*/), archiving::file_type::recording, "wav"));

		// monitor folder for info files
		// Assumes info file filename format ends with [uuid].xml
		if(app::confMap["info.enabled"].as<bool>())
			inotify_monitor->add_directory(
					info_directory,
					app::inotify::event::created | app::inotify::event::moved_to | app::inotify::event::closed,
					boost::bind(&archiving::cdr_dir_monitor::handle_event, boost::ref(cdr_dir_monitor), _1, _2,
							size_t(36/*uuid*/+4/*.xml*/), archiving::file_type::info, "xml"));

		app::archivable_dedup archivable_dedup(archivable_catalogue, internal_db);
		boost::function<archiving::archivable::pointer(const archiving::archivable::pointer&)> fn_get_loaded_instance(
						boost::bind(&app::archivable_dedup::get_loaded_instance, boost::ref(archivable_dedup), _1));

		// initialise the internal static variables before we load anything;
		internal_db.find_archivables(app::hostname, fn_get_loaded_instance, boost::bind(fn_in_queue_produce, _1, true), more_to_load_from_db, 1.0);

		// setup directory readers / loaders ( extra safety net if by any chance the inotify fails )
		// also useful when we have to start collectd after letting it rest for a while
		app::directory_loader dir_loader(app::confMap, app::quit);

		// one for CDRs
		dir_loader_threads.create_thread(
				boost::bind(&app::directory_loader::run, boost::ref(dir_loader),
						"CDR", cdr_directory,
						boost::protect(
								boost::bind(&archiving::cdr_dir_monitor::add_path, boost::ref(cdr_dir_monitor),
										_1, size_t(36/*uuid*/+4/*.cdr*/+4/*.xml*/), archiving::file_type::cdr, "xml")),
						boost::protect(
								boost::bind(&app::archivable_dedup::is_file_loaded, boost::ref(archivable_dedup),
										archiving::file_type::cdr, size_t(36/*uuid*/+4/*.cdr*/+4/*.xml*/), _1)),
						boost::protect(continue_loading_dir_callback)));

		// One for PCaps
		if(app::confMap["pcap.enabled"].as<bool>())
			dir_loader_threads.create_thread(
					boost::bind(&app::directory_loader::run, boost::ref(dir_loader),
							"PCap", pcap_directory,
							boost::protect(
									boost::bind(&archiving::cdr_dir_monitor::add_path, boost::ref(cdr_dir_monitor),
											_1, identify_pcap_type)),
							boost::protect(
									boost::bind(&app::archivable_dedup::is_file_loaded, boost::ref(archivable_dedup),
											identify_pcap_type, _1)),
							boost::protect(continue_loading_dir_callback)));

		// One for chunks
		if(app::confMap["chunk.enabled"].as<bool>())
			dir_loader_threads.create_thread(
					boost::bind(&app::directory_loader::run, boost::ref(dir_loader),
							"chunk", chunk_directory,
							boost::protect(
									boost::bind(&archiving::cdr_dir_monitor::add_path, boost::ref(cdr_dir_monitor),
											_1, size_t(36/*uuid*/+4/*.wav*/), archiving::file_type::recording, "wav")),
							boost::protect(
									boost::bind(&app::archivable_dedup::is_file_loaded, boost::ref(archivable_dedup),
											archiving::file_type::recording, size_t(36/*uuid*/+4/*.wav*/), _1)),
							boost::protect(continue_loading_dir_callback)));

		// One for info files
		if(app::confMap["info.enabled"].as<bool>())
			dir_loader_threads.create_thread(
					boost::bind(&app::directory_loader::run, boost::ref(dir_loader),
							"info", info_directory,
							boost::protect(
									boost::bind(&archiving::cdr_dir_monitor::add_path, boost::ref(cdr_dir_monitor),
											_1, size_t(36/*uuid*/+4/*.xml*/), archiving::file_type::info, "xml")),
							boost::protect(
									boost::bind(&app::archivable_dedup::is_file_loaded, boost::ref(archivable_dedup),
											archiving::file_type::info, size_t(36/*uuid*/+4/*.xml*/), _1)),
							boost::protect(continue_loading_dir_callback)));

		// setup the cdr inspector; one that makes sense of variables from CDR to find recordings
		app::cdr_inspector cdr_inspector(
				fn_in_queue_consume, fn_out_queue_produce, fn_remove_from_catalogue, fn_is_in_catalogue,
				fn_save_to_database, fn_add_put_group, app::confMap, app::hostname);
		for(unsigned int i = app::confMap["cdr_handler.number_of_threads"].as<unsigned int>(); i > 0; --i)
			cdr_inspector_threads.create_thread(boost::bind(&app::cdr_inspector::run, boost::ref(cdr_inspector)));

		const unsigned n_service_threads = boost::thread::hardware_concurrency() * 2 + 1;
		LogMessage("Starting " << n_service_threads << " service threads");
		for(unsigned int i = 0; i < n_service_threads; i++) {
			threads.create_thread(boost::bind(&boost::asio::io_service::run, boost::ref(io_service)));
		}

		conn_mgr->start();

		const unsigned n_send_schedulers = 2;
		LogMessage("Starting " << n_send_schedulers << " scheduler threads");
		for(unsigned int i = 0; i < n_send_schedulers; i++) {
			threads.create_thread(boost::bind(&collectd::net::archived_client::run, archived_client));
		}

		// setup the save_run thread
		threads.create_thread(boost::bind(&app::database::internal::save_run, boost::ref(internal_db)));

		// kick the monitor to life
		inotify_monitor->async_monitor();

		DevDebug("app::confMap[\"purge.done_items_max_age_minutes\"].as<unsigned int>() "<<app::confMap["purge.done_items_max_age_minutes"].as<unsigned int>());

		// kick off the purge thread
		threads.create_thread(boost::bind(app::purge,
				boost::ref(internal_db), boost::ref(app::quit), boost::ref(app::hostname),
				app::confMap["purge.done_items_max_age_minutes"].as<unsigned int>(),
				app::confMap["purge.max_age_days"].as<unsigned int>(),
				app::confMap["purge.batch_size"].as<unsigned int>(),
				app::confMap["purge.frequency_minutes"].as<unsigned int>()));

		int ticks = 1;
		while(!app::quit) {
			if(app::confMap["app.tik_tok"].as<std::string>() == "true") {
				LogDebug(((time(0) % 2) ? "tik" : "tok"));
			}

			if(more_to_load_from_db && !(ticks % 10) && !app::quit) {
				boost::system::error_code ec;
				float pool_level = conn_mgr->get_pool_level(ec);
				if (!ec && pool_level > 0) {
					internal_db.find_archivables(app::hostname, fn_get_loaded_instance, boost::bind(fn_in_queue_produce, _1, true), more_to_load_from_db, pool_level);
				} else {
					LogMessage("Database loading paused; waiting for ArchiveDs to come back online");
				}
			}

			if (ticks % 15 == 0 && !app::quit) {
				try {
					put_group_saver_list.save();
                    put_group_cache.prune();
                    internal_db.put_group_purge_expired();
				} catch (const std::exception& e) {
					LogError(e.what());
				}
			}

			if(!(ticks % 60) && !app::quit) {
				LogMessage("[statistics] Queue:"<<*in_queue<<' '<<in_queue->get_statistics());
				LogMessage("[statistics] Queue:"<<*out_queue<<' '<<out_queue->get_statistics());
				LogMessage("[statistics] DBLoading:"<<RML_YES_NO(more_to_load_from_db));
				LogDebug("[statistics] SQLIteSessions:"<<rml::sqlite::session::count());
				LogDebug("[statistics] SQLIteSessionPoolSize:"<<internal_db.get_pool_size());

				internal_db.clear_dormant_sessions();

				rml::sqlite::status::log_all();
			}

			if (!app::quit) {
				boost::this_thread::sleep_for(boost::chrono::seconds(1));
				ticks++;
			}
		}

		// First one to start shutting down as we need time for any on-going PUTs that are almost done to succeed.
		LogMessage("Stopping ArchiveD client");
		archived_client->shutdown();

		LogMessage("Stopping ArchiveD connection manager");
		conn_mgr->shutdown();

		LogMessage("Stopping inotify monitor(s)");
		inotify_monitor.reset();

		LogMessage("Waiting for directory-loaders to finish");
		dir_loader_threads.join_all();

		LogMessage("Setting IN queue to finish");
		in_queue->set_finishing();

		LogMessage("Waiting for cdr-inspectors to finish");
		cdr_inspector_threads.join_all();

		LogMessage("Setting OUT queue to finish");
		out_queue->set_finishing();

        LogMessage("Reset archived_client");
		archived_client.reset();

        LogMessage("Shutting down put-group saver-list");
		put_group_saver_list_type::list_citr_range_type remaining = put_group_saver_list.shutdown();
		LogMessage(std::distance(remaining.begin(), remaining.end()) << " found (unsaved) in put-group saver-list");

		p_work.reset();

		io_service.stop();

		LogMessage("Shutting down internal-db");
		internal_db.shutdown();

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
