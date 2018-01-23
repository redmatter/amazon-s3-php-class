//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 08/12/2017.
//

#ifndef ARCHIVING_ESTUARY_APP_ARCHIVED_SHIM_HPP
#define ARCHIVING_ESTUARY_APP_ARCHIVED_SHIM_HPP

#include "../../common/archivable.hpp"
#include "error.hpp"
#include "utils.hpp"
#include "archivable_queue.hpp"
#include "http/server.hpp"
#include "http/request_wrapped.hpp"
#include "http/pending_request.hpp"
#include "put_object_request.hpp"
#include "http/pending_request_map.hpp"

#include <rml/logger.h>
#include <rml/utils/snowflake.hpp>
#include <rml/utils.h>

#include <boost/atomic.hpp>
#include <boost/exception/all.hpp>
#include <boost/throw_exception.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/range/iterator_range.hpp>

namespace app
{

struct request_status
{
    enum enum_t
    {
        processing = 0,
        processed_ok,
        processed_error,
        response_sent,
        connection_closed,

        n_enums
    };

    static const char* to_string(const enum_t e)
    {
        static const char* strings[n_enums] = {
                "processing",
                "processed-ok",
                "processed-error",
                "response-sent",
                "connection-closing"
        };

        if (e >= 0 && e < n_enums) {
            return strings[e];
        }

        return "unknown";
    }
};

struct archived_shim
{
    rml::utils::snowflake& snowflake;

    const boost::shared_ptr<archivable_queue> send_queue;
    const boost::shared_ptr<response_queue> responder_queue;

    http::pending_request_map pending_requests;

    boost::atomic_bool shutting_down;

    static void describe_config(boost::program_options::options_description& desc)
    {
        desc.add_options()
                ("http_server.listen_address", boost::program_options::value<std::string>()->default_value("0.0.0.0"),
                 "HTTP listen address")
                ("http_server.listen_port", boost::program_options::value<std::string>()->default_value("80"),
                 "HTTP listen port");
    }

    explicit archived_shim(rml::utils::snowflake& snowflake, const boost::shared_ptr<archivable_queue>& send_queue,
                           const boost::shared_ptr<response_queue>& responder_queue) :
            snowflake(snowflake), send_queue(send_queue), responder_queue(responder_queue), shutting_down(false)
    {}

    void read_request_body_to_stream(
            const boost::iterator_range<const char*>& bytes_range,
            const boost::system::error_code& ec,
            const size_t& bytes_count,
            const boost::shared_ptr<http::connection_type>& connection,

            size_t& bytes_remaining,
            const boost::shared_ptr<std::iostream>& stream,
            const http::request_correlation& correlation,
            const boost::function<void(bool)> completion_callback)
    try
    {
        if (ec || connection->has_error()) {
            LogMessage(correlation << " Error while reading request; ErrorCode:'" << ec << "' ConnectionError:"
                                   << RML_YES_NO(connection->has_error()));
            completion_callback(false);
            return;
        }

        stream->write(bytes_range.begin(), bytes_count);
        bytes_remaining -= bytes_count;
        if (bytes_remaining > 0) {
            // If we are to handle too many requests, resulting in too many open file-handles, consider closing
            // the file-stream here, and re-opening before the next write (in append-mode)

            // if there are more to get from the request, keep reading
            connection->read(
                    boost::bind(&archived_shim::read_request_body_to_stream, this, _1, _2, _3, _4,
                                bytes_remaining, stream, correlation, boost::protect(completion_callback)));
        } else {
            completion_callback(true);
        }
    } catch (const std::exception& e) {
        LogError(correlation << " Error while reading request; " << e.what());
        // completion_callback(false);
    }

    void enqueue_put_archivable(const boost::shared_ptr<archiving::archivable> archivable,
                                const boost::shared_ptr<const http::pending_request> pending_request)
    try
    {
        archivable->set_database_id(pending_request->correlation.id);

        boost::system::error_condition error;
        send_queue->produce(archivable, boost::chrono::seconds(10), error);
        if (error) {
            const bool is_busy_or_shutting_down(error.value() == queue::error::runtime_errors::is_full
                                                || error.value() == queue::error::runtime_errors::is_finishing);
            if (is_busy_or_shutting_down) {
                boost::throw_exception(
                        boost::enable_error_info(error::http_error_exception())
                                << error::error_info_request_correlation(pending_request->correlation)
                                << error::error_info_error_condition(
                                        boost::system::error_condition(error::http::service_unavailable))
                                << error::error_info_internal_error_message(error.message()));
            }

            boost::throw_exception(
                    boost::enable_error_info(error::internal_error_exception())
                            << error::error_info_request_correlation(pending_request->correlation)
                            << error::error_info_error_condition(error));
        }
    } catch (const error::http_error_exception& e) {
        respond(e);
    } catch (const std::exception& e) {
        respond(e);
    }

    inline
    bool respond(const std::exception& e)
    {
        http::request_correlation const* e_correlation = boost::get_error_info<error::error_info_request_correlation>(e);
        if (!e_correlation) {
            LogError("Request-correlation NOT found in exception");
            return false;
        }

        const http::request_correlation& correlation(*e_correlation);

        LogError(correlation << " Exception: " << e.what());

        std::string const* internal_error_message = boost::get_error_info<error::error_info_internal_error_message>(e);
        if (internal_error_message) {
            LogError(correlation << " Error: " << *internal_error_message);
        }

        boost::system::error_condition const* error_condition =
                boost::get_error_info<error::error_info_error_condition>(e);
        if (error_condition) {
            LogError(correlation << " Error: " << *error_condition);
        }

        boost::system::error_condition error;
        responder_queue->produce(boost::make_shared<response>(correlation.id,
                                                              boost::system::error_condition(
                                                                      error::http::internal_server_error)), error);
        if (error) {
            LogError(correlation << " Failed to setup error-response; " << error);
            return false;
        }

        return true;
    }

    /**
     * @brief Initiate shutdown; make sure all requests are responded to!
     */
    void shutdown()
    {
        shutting_down = true;

        // If there are objects in send_queue, we need to respond "Service Unavailable" ASAP
        while (send_queue->size()) {
            boost::shared_ptr<archiving::archivable> item;
            boost::system::error_condition ec;
            send_queue->consume(item, boost::chrono::seconds(1), ec);
            if (ec) {
                LogMessage(ec);
                break;
            }

            LogMessage(item->get_database_id() << " Cancelled on shutdown; " << ec);

            ec.clear();
            responder_queue->produce(
                    boost::make_shared<response>(item->get_database_id(), boost::system::error_condition(
                            error::http::service_unavailable)), ec);
            if (ec) {
                LogError(item->get_database_id() << " Failed to setup error-response; " << ec);
            }
        }

        send_queue->set_finishing();

        while (responder_queue->size()) {
            LogMessage("Waiting for responder_queue to be empty");
            boost::this_thread::sleep_for(boost::chrono::seconds(1));
        }

        responder_queue->set_finishing();
    }

    /**
     * @brief Prepare response and send it!
     */
    void run_responder()
    {
        while (true) {
            try {
                boost::shared_ptr<response> res;
                boost::system::error_condition error;
                responder_queue->consume(res, error);
                if (!error) {
                    boost::shared_ptr<const http::pending_request> pending_request =
                            pending_requests.lookup(res->request_id);
                    if (pending_request) {
                        LogMessage(pending_request->correlation << " Result: " << *res);
                        finalise_request(pending_request, res->upshot, res->message);
                    } else {
                        LogError("Could not find pending-request-instance for request-id:" << res->request_id);
                    }
                } else if (error == queue::error::make_error_condition(queue::error::runtime_errors::is_finishing)) {
                    break;
                } else {
                    LogError(error);
                }
            } catch (const std::exception& e) {
                LogError(e.what());
            }
        }
    }

    inline
    void finalise_request(const boost::shared_ptr<const http::pending_request>& pending_request,
                          const boost::system::error_condition& ec,
                          const std::string& message)
    {
        if (ec.category() != error::get_http_category()) {
            LogError(pending_request->correlation << ' ' << error::http::internal_server_error << " Internal Error: "
                                                  << message << " (" << ec << ')');
            finalise_request(pending_request, error::http::internal_server_error,
                             (message.empty() ? "Internal Error" : message));
        } else {
            finalise_request(pending_request, static_cast<error::http::enum_t>(ec.value()), message);
        }
    }

    inline
    std::string render_response(const error::http::enum_t status_code, const std::string& message)
    {
        std::ostringstream ostr;
        if (error::http::has_response_body(status_code)) {
            boost::property_tree::ptree response;
            response.put("error", error::get_http_category().message(status_code));
            response.put("message", message);

            boost::property_tree::write_json(ostr, response);
        }

        return ostr.str();
    }

    inline
    void finalise_request(const boost::shared_ptr<const http::pending_request>& pending_request,
                          const error::http::enum_t status_code,
                          const std::string& message)
    {
        if (!pending_request->respond(status_code, "application/json", render_response(status_code, message))) {
            LogError(pending_request->correlation << " Failed to send response");
        }

        pending_requests.remove(pending_request);
    }

    inline
    bool respond(const error::http_error_exception& e)
    {
        http::request_correlation const* e_correlation = boost::get_error_info<error::error_info_request_correlation>(e);
        if (!e_correlation) {
            LogError("Request-correlation NOT found in exception");
            return false;
        }

        const http::request_correlation& correlation(*e_correlation);

        std::string const* internal_error_message = boost::get_error_info<error::error_info_internal_error_message>(e);
        if (internal_error_message) {
            LogError(correlation << " Error: " << *internal_error_message);
        }

        boost::system::error_condition const* ec = boost::get_error_info<error::error_info_error_condition>(e);
        if (!ec) {
            LogError(correlation << " http_error_exception without error_condition");
            return false;
        }

        LogVerbose(correlation << " Error: " << *ec);

        boost::system::error_condition error;
        responder_queue->produce(boost::make_shared<response>(correlation.id, *ec, e.what()), error);
        if (error) {
            LogError(correlation << " Failed to setup error-response; " << error);
            return false;
        }

        return true;
    }

    void respond_internal_server_error(http::server::connection_ptr connection)
    {
        http::send_response(connection, error::http::internal_server_error, "text/plain", "Internal Server Error");
    }

    void operator()(http::server::request const& request, http::server::connection_ptr connection)
    try
    {
        const rml::utils::snowflake::id_t& id(snowflake.make_id());

        boost::shared_ptr<http::pending_request> pending_request =
                boost::make_shared<http::pending_request>(id, request, connection);

        if (shutting_down == true) {
            pending_request->respond(error::http::service_unavailable);
            return;
        }

        try {
            LogMessage(pending_request->correlation
                               << ' ' << pending_request->request.get_method()
                               << ' ' << pending_request->request.get_destination() << " Handling request");

            pending_requests.add(pending_request);

            boost::shared_ptr<const put_object_request> put_request =
                    pending_request->make_shared_data_from_request<put_object_request>();

            // https://stackoverflow.com/a/8880116/83005
            if (pending_request->request.expect_100_continue()) {
                if (!pending_request->request.respond(error::http::continue_)) {
                    LogMessage(pending_request->correlation << " Failed to send 'Continue'");
                    pending_requests.remove(pending_request);
                    return;
                }
            }

            connection->read(
                    boost::bind(&archived_shim::read_request_body_to_stream, this, _1, _2, _3, _4,
                                boost::ref(put_request->bytes_remaining), put_request->get_iostream(),
                                boost::cref(pending_request->correlation),
                                boost::protect(boost::bind(&put_object_request::enqueue_archivable, put_request, _1,
                                                           boost::protect(
                                                                   boost::bind(&archived_shim::enqueue_put_archivable,
                                                                               this, _1, pending_request))))));
        } catch (const error::http_error_exception& e) {
            respond(e);
        }
    } catch (const std::exception& e) {
        LogError("Error while accepting a request; Error: " << e.what());
        respond_internal_server_error(connection);
    }
};

} // namespace app

#endif //ARCHIVING_ESTUARY_APP_ARCHIVED_SHIM_HPP
