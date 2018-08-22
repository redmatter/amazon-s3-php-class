//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 20/11/2017.
//

#include "app/error.hpp" // work around for "‘make_error_condition’ was not declared in this scope"
#include "defines.h"
#include "app/config/common.hpp"
#include "app/config/command_line.hpp"
#include "../common/archived_client.hpp"
#include "app/archivable_queue.hpp"
#include "app/archived_shim.hpp"

#include <rml/logger.h>
#include <rml/utils.h>
#include <rml/hapi.hpp>
#include <rml/version.h>
#include <rml/daemon.h>
#include <rml/config.h>
#include <rml/utils/snowflake.hpp>
#include <rml/utils/cached_resolver.hpp>

#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/chrono/chrono.hpp>
#include <boost/bind/protect.hpp>

#include <list>

namespace
{
typedef std::list<boost::function0<void> > shutdown_hooks_list_type;

bool initialisation_complete = false;

} // namespace

namespace app
{
// time to quit?
volatile bool quit = false;

// was HUPed
volatile bool wasHUPed = false;

} // namespace app

#ifdef GPERFTOOLS_PROFILE_CPU
/**
 * GPerfTools seem alergic to signals; so we need to flag quit by other means
 */
void geoshim_stop_handler(const boost::shared_ptr< api::request >& request)
{
    // flag quit
    app::quit = true;

    api::proxy_server::response_header headers[] = {
            {"Connection","close"},
            {"Content-Type", "application/json"}};

    request->conn->set_status(api::proxy_server::connection::ok);
    request->conn->set_headers(boost::make_iterator_range(headers, headers+2));
    request->conn->write("{ \"status\": \"OK\" }");
}
#endif // def GPERFTOOLS_PROFILE_CPU

#ifdef RML_LOG_RUNTIME_LEVEL_CHANGE

/**
 * Toggle logging up or down; note that the real max-level is decided by compile options
 * We have the ability to remove debug log lines altogether from the compiled binary
 */
inline void toggle_debug()
{
    static bool debug_on(false);
    debug_on = !debug_on;
    rml::logger::set_temp_level(debug_on ? rml::logger::level::debug : rml::logger::level::no_logging);

    LogMessage("Temporary Debug ON ? " << RML_YES_NO(debug_on));
}

#endif // def RML_LOG_RUNTIME_LEVEL_CHANGE

void initiate_shutdown(const shutdown_hooks_list_type& hooks)
{
    for (shutdown_hooks_list_type::const_iterator i = hooks.begin(); i != hooks.end(); ++i) {
        (*i)();
    }

    app::quit = true;
}

/**
 * Log shutdown event
 */
void shutdown_hook(bool& shutdown_flag)
{
    shutdown_flag = true;
    LogMessage("Shutting down...");
}

/**
 * Signal handling via boost::asio
 *
 * @param error
 * @param signal
 * @param signals
 */
void handle_signals(const boost::system::error_code& error, int signal, boost::asio::signal_set& signals,
                    const shutdown_hooks_list_type& shutdown_hooks)
{
    if (!error) {
        switch (signal) {
        case SIGINT:
        case SIGTERM: {
            initiate_shutdown(shutdown_hooks);
        }
            break;

        case SIGHUP: {
            app::wasHUPed = true;
        }
            break;

#ifdef RML_LOG_RUNTIME_LEVEL_CHANGE
        case SIGUSR1: {
            toggle_debug();
        }
            break;
#endif
        case SIGUSR2:
            try {
                LogMessage("SIGUSR2: reloading handler-config");
                // reload_cache();
            } catch (const std::exception& e) {
                LogCritical("SIGUSR2: handler-config reload failed; error:" << e.what());
                initiate_shutdown(shutdown_hooks);
            }
            break;

        default: // ignore any other signal
            break;
        }
    } else {
        LogError(error.message());
    }

    if (!app::quit && error != boost::asio::error::operation_aborted) {
        signals.async_wait(boost::bind(handle_signals, _1, _2, boost::ref(signals), boost::cref(shutdown_hooks)));
    }
}

/**
 * The Main
 *
 * @param argc
 * @param argv
 * @return
 */
int main(int argc, const char* argv[]) try
{
    shutdown_hooks_list_type shutdown_hooks;

    boost::system::error_code ec_ignore;
    const boost::chrono::steady_clock::time_point startup_time(boost::chrono::steady_clock::now(ec_ignore));

    // Initialise logger (default setup, reinitialised later) so that any initialization error gets logged
    rml::logger::configure();

    rml::hapi::host_cache::initialise();

    std::string pid_file;
    boost::program_options::variables_map config;

    try {
        app::config::command_line cli_config(argc, argv);

        if (cli_config.is_opted("help")) {
            cli_config.display_help(std::cout);
            exit(rml::utils::exit_codes::normal);
        }

        boost::program_options::options_description options_desc(
                boost::str(boost::format(DEFAULT_APP_LONG_NAME " v%1%\n%2%\noptions")
                           % version_number_string % copyright_string));

        /* describe config here */
        rml::daemon::describe_config(options_desc);
        rml::logger::describe_config(options_desc);
        app::config::common::describe_config(options_desc);
        app::archived_client::describe_config(options_desc);
        app::archived_shim::describe_config(options_desc);

        options_desc.add_options()
                ("estuary.archived_connections", boost::program_options::value<unsigned>()->default_value(2),
                 "Number of connections to ArchiveD");

        if (cli_config.is_opted("help-conf")) {
            std::cout << options_desc << "\n";
            exit(rml::utils::exit_codes::normal);
        }

        if (cli_config.is_opted("version")) {
            std::cout << boost::str(
                    boost::format(DEFAULT_APP_LONG_NAME" v%1%\n%2%\n") % version_number_string % copyright_string);
            exit(rml::utils::exit_codes::normal);
        }

        rml::config::parse(cli_config.get_option<std::string>("conf"), options_desc, config);

        rml::logger::configure(config, SYSLOG_APP_NAME);

        LogMessage(DEFAULT_APP_LONG_NAME
                           " v" << version_number_string);
        LogMessage(copyright_string << " built on " << version_date << ' ' << build_host);

        pid_file = cli_config.get_option<std::string>("pid");
    } catch (const std::exception& e) {
        LogError("Initialisation Error - exception: " << e.what());
        exit(rml::utils::exit_codes::initialization_error);
    }

    try {
        bool shutdown_flag = false;

        boost::shared_ptr<boost::asio::io_service> p_io_service = boost::make_shared<boost::asio::io_service>();
        boost::shared_ptr<boost::asio::io_service::work> p_work = boost::make_shared<boost::asio::io_service::work>(
                boost::ref(*p_io_service));

        boost::thread_group io_service_threads;
        for (unsigned int n_threads = std::max(config["common.io_service_threads"].as<unsigned>(),
                                               boost::thread::hardware_concurrency()); n_threads > 0; n_threads--) {

            io_service_threads.create_thread(boost::bind(&boost::asio::io_service::run, p_io_service));
        }

        // set the signal handlers
        boost::asio::signal_set signals(*p_io_service);
        signals.add(SIGINT);
        signals.add(SIGTERM);
        signals.add(SIGHUP);
#ifdef RML_LOG_RUNTIME_LEVEL_CHANGE
        signals.add(SIGUSR1);
#endif
        signals.add(SIGUSR2);
        signals.add(SIGPIPE);

        shutdown_hooks.push_back(boost::bind(shutdown_hook, boost::ref(shutdown_flag)));

        signals.async_wait(boost::bind(handle_signals, _1, _2, boost::ref(signals), boost::cref(shutdown_hooks)));

        rml::daemon::initialise(config, pid_file);
        app::config::common common_config(config);
        initialisation_complete = true;

        rml::utils::snowflake snowflake(313); // just a happy prime (magic number)

        boost::shared_ptr<rml::utils::cached_resolver> resolver =
                boost::make_shared<rml::utils::cached_resolver>(5, 5, 2);

        const unsigned int archived_connections = std::max(
                config["estuary.archived_connections"].as<unsigned int>(), 2U);

        boost::shared_ptr<app::archivable_queue> archivable_queue =
                boost::make_shared<app::archivable_queue>(archived_connections * 2);
        boost::shared_ptr<app::response_queue> response_queue = boost::make_shared<app::response_queue>(0);

        boost::shared_ptr<boost::thread_group> http_server_threads = boost::make_shared<boost::thread_group>();
        boost::shared_ptr<boost::asio::io_service> http_server_io_service =
                boost::make_shared<boost::asio::io_service>();

        app::archived_shim archived_shim(snowflake, archivable_queue, response_queue);
        boost::shared_ptr<app::http::server> http_server = boost::make_shared<app::http::server>(
                app::http::server::options(archived_shim)
                        .address(config["http_server.listen_address"].as<std::string>())
                        .port(config["http_server.listen_port"].as<std::string>())
                        .thread_pool(
                                boost::make_shared<boost::network::utils::thread_pool>(
                                        std::max<unsigned>(boost::thread::hardware_concurrency() / 2, 2),
                                        http_server_io_service, http_server_threads))
                        .reuse_address(true));

        // responder thread; it only enqueues on to cpp-netlib's internal threadpool, so one thread should do
        boost::shared_ptr<boost::thread> responder_thread(http_server_threads->create_thread(
                boost::bind(&app::archived_shim::run_responder, boost::ref(archived_shim))));

        const boost::function<void(const uint64_t, app::http::request_progress::enum_t)> fn_mark_progress =
                boost::bind(&app::archived_shim::mark_request_progress_by_id, boost::ref(archived_shim), _1, _2);

        const common::collectd::fn_produce_type fn_error_callback =
                boost::bind(app::produce_response_on_error, response_queue, _1, _2);
        const common::collectd::fn_consume_with_timeout_finishing_callback_type fn_get_archivable_callback =
                boost::bind(app::consume_archivable, archivable_queue, _2, _1, _3, _4, boost::protect(fn_mark_progress));
        const common::collectd::fn_remove_from_catalogue_type fn_dummy_remove_from_catalogue =
                boost::bind(app::dummy_remove_from_catalogue, _1);
        const common::collectd::fn_save_to_database_type fn_dummy_save_to_database =
                boost::bind(app::dummy_save_to_database, _1);
        const common::collectd::fn_on_successful_put_type fn_on_successful_put =
                boost::bind(app::produce_response_on_success, response_queue, _1, boost::protect(fn_mark_progress));

        std::list<boost::shared_ptr<app::archived_client> > archived_clients;
        for (unsigned int i = 0; i < archived_connections; i++) {
            boost::shared_ptr<app::archived_client> archived_client = boost::make_shared<app::archived_client>(
                    boost::ref(*p_io_service), fn_error_callback, fn_get_archivable_callback,
                    fn_dummy_remove_from_catalogue, fn_dummy_save_to_database,
                    config["archived_client.server_host"].as<std::string>(),
                    config["archived_client.server_port"].as<std::string>(),
                    fn_on_successful_put);
            archived_client->connect_archived();

            archived_clients.push_back(archived_client);
        }

        for (unsigned int n_threads = std::max<unsigned>(boost::thread::hardware_concurrency() / 2, 2);
             n_threads > 0; n_threads--) {

            http_server_threads->create_thread(boost::bind(&app::http::server::run, http_server));
        }

        LogMessage("Initialisation complete");

        // ------------- stuff happens here! ------------- //

        while (!shutdown_flag) {
            // const boost::chrono::steady_clock::time_point& now(boost::chrono::steady_clock::now());

            boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));
        }

        LogMessage("Shutting down");

        shutdown_hooks.clear();

        archived_shim.shutdown();

        {
            std::list<app::archived_client::pointer>::iterator end = archived_clients.end();
            std::list<app::archived_client::pointer>::iterator itr = archived_clients.begin();
            for (; itr != end; ++itr) {
                (*itr)->shutdown();
            }
        }
        // delete archived-clients so that any in the middle of state-machine / callbacks / etc will self-destroy,
        // as soon as they finish what those. Here we clear the list that holds a shared_ptr to each client, and wait
        // up to ~10s before continuing with shutdown.
        archived_clients.clear();
        {
            const boost::chrono::steady_clock::time_point wait_until(
                    boost::chrono::steady_clock::now() + boost::chrono::seconds(10));
            while (wait_until >= boost::chrono::steady_clock::now()) {
                const unsigned int& instances(app::archived_client::get_instances_count());
                if (instances == 0) {
                    LogMessage("All ArchiveD-clients finished");
                    break;
                }

                LogMessage("Waiting for ArchiveD-clients to finish;" << instances << " instances remaining");
                boost::this_thread::sleep_for(boost::chrono::milliseconds(500));
            }
        }

        if (app::archived_client::get_instances_count()) {
            LogWarning("ArchiveD-clients did not finish while waiting; continuing with shutdown");
        }

        p_work.reset();
        p_io_service->stop();

        io_service_threads.join_all();

        responder_thread->join();

        http_server->stop();

        LogMessage("Terminated normally");

        rml::logger::deconfigure();

        exit(rml::utils::exit_codes::normal);
    } catch (const std::exception& e) {
        LogError("exception: " << e.what());
        LogMessage("Abnormal program termination");
        exit(initialisation_complete ? rml::utils::exit_codes::failure : rml::utils::exit_codes::initialization_error);
    }
} catch (const std::exception& e) {
    LogError("Exception: " << e.what());
    LogMessage("Abnormal program termination");
    exit(rml::utils::exit_codes::failure);
}
