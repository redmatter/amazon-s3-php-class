//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 20/11/2017.
//

#include "defines.h"
#include "app/config/common.hpp"
#include "app/config/command_line.hpp"

#include <rml/logger.h>
#include <rml/utils.h>
#include <rml/hapi.hpp>
#include <rml/version.h>
#include <rml/daemon.h>
#include <rml/config.h>
#include <rml/state_reactor.hpp>

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

template<typename FlagType>
void callback_set_flag(boost::atomic<FlagType>& flag, const FlagType& value, const char* message)
{
    flag = value;
    if (message) {
        LogMessage(message << " [value: " << value << "]");
    }
}

template<typename FlagType>
void state_reactor_callback_set_flag(boost::atomic<FlagType>& flag, const FlagType& value, const char* message,
                                     const bool& outcome)
{
    flag = value;
    if (message) {
        if (outcome) {
            LogVerbose(message << " [value: " << value << " outcome:ok]");
        } else {
            LogWarning(message << " [value: " << value << " outcome:failure]");
        }
    } else if (!outcome) {
        LogWarning("FIXME: Unknown callback with error [outcome:failure]");
    }
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
        case SIGTERM: { initiate_shutdown(shutdown_hooks); }
            break;

        case SIGHUP: { app::wasHUPed = true; }
            break;

#ifdef RML_LOG_RUNTIME_LEVEL_CHANGE
        case SIGUSR1: { toggle_debug(); }
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
 * Main!
 *
 * @param argc
 * @param argv
 * @return
 */
int main(int argc, const char* argv[]) try
{
    bool shutdown_flag = false;
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
                boost::str(boost::format(DEFAULT_APP_LONG_NAME" v%1%\n%2%\noptions")
                           % version_number_string % copyright_string));

        /* describe config here */
        rml::daemon::describe_config(options_desc);
        rml::logger::describe_config(options_desc);
        app::config::common::describe_config(options_desc);
        /* describe config here */

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
        boost::shared_ptr<boost::asio::io_service> p_io_service = boost::make_shared<boost::asio::io_service>();
        boost::shared_ptr<boost::asio::io_service::work> p_work = boost::make_shared<boost::asio::io_service::work>(
                boost::ref(*p_io_service));

        boost::thread_group io_service_threads;
        {
            unsigned int n_cores = boost::thread::hardware_concurrency();
            while (0 < n_cores--) {
                io_service_threads.create_thread(boost::bind(&boost::asio::io_service::run, p_io_service));
            }
        }

#ifndef GPERFTOOLS_PROFILE_CPU
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
#else
        LogCoutIf(true,
                  "While profiling, do not press Ctrl+C or use kill command to stop geoshim.\n"
                          "Visit the URL below instead.\n"
                          "    http://127.0.0.0:"<<app::confMap["api.proxy_listen_port"].as<std::string>()<<"/geoshim/stop");
#endif // ifndef GPERFTOOLS_PROFILE_CPU

        rml::daemon::initialise(config, pid_file);
        app::config::common common_config(config);
        initialisation_complete = true;
        LogMessage("Initialisation complete");

        // ------------- stuff happens here! ------------- //

        boost::chrono::steady_clock::time_point task_loading_time(startup_time);
        boost::atomic_bool task_loading_in_progress(false);

        while (!shutdown_flag) {
            const boost::chrono::steady_clock::time_point& now(boost::chrono::steady_clock::now());

//            if ((task_loading_time + boost::chrono::seconds(10)) < now && !task_loading_in_progress) {
//                task_loading_time = now;
//                task_loading_in_progress = true;
//                // set-off loading
//                LogVerbose("Task loading setup");
//                state_reactor.schedule(
//                        boost::bind(app::load_task, cdrdb,
//                                    boost::protect(boost::bind(app::enqueue_task, task_queue, _1))),
//                        boost::bind(callback_atomic_flag_status < bool > ,
//                                    boost::ref(task_loading_in_progress), false, "Task loading finished", _1),
//                        state_reactor_type::priority::high);
//            }

            boost::this_thread::sleep_for(boost::chrono::milliseconds(500));
        }

        // ------------- tearing down from here! ------------- //

        // todo review shutdown-procedure

        shutdown_hooks.clear();

        p_work.reset();
        p_io_service->stop();

        io_service_threads.join_all();

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
