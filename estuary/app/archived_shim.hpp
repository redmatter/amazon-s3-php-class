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

#include <rml/utils/snowflake.hpp>
#include <rml/utils.h>
#include <rml/logger.hpp>

#include <boost/network/protocol/http/server.hpp>
#include <boost/unordered_map.hpp>
#include <boost/atomic.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/optional.hpp>
#include <boost/optional/optional_io.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/exception/all.hpp>
#include <boost/throw_exception.hpp>
#include <boost/regex.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/container/slist.hpp>
#include <boost/range/iterator_range.hpp>

#include <cstring>

namespace app
{

struct archived_shim;
typedef boost::network::http::async_server<archived_shim> http_server;
typedef http_server::request http_request_type;
typedef http_server::connection http_connection_type;
typedef http_server::response_header http_response_header_type;
typedef http_server::connection_ptr http_connection_ptr_type;
typedef http_connection_type::status_t http_status_type;

bool to_netlib_status(const error::http::enum_t e, http_status_type& status)
{
    switch (e) {
    case error::http::ok:
        status = http_connection_type::ok;
        break;
    case error::http::created:
        status = http_connection_type::created;
        break;
    case error::http::accepted:
        status = http_connection_type::accepted;
        break;
    case error::http::no_content:
        status = http_connection_type::no_content;
        break;

    case error::http::multiple_choices:
        status = http_connection_type::multiple_choices;
        break;
    case error::http::moved_permanently:
        status = http_connection_type::moved_permanently;
        break;
    case error::http::moved_temporarily:
        status = http_connection_type::moved_temporarily;
        break;
    case error::http::not_modified:
        status = http_connection_type::not_modified;
        break;

    case error::http::bad_request:
        status = http_connection_type::bad_request;
        break;
    case error::http::unauthorized:
        status = http_connection_type::unauthorized;
        break;
    case error::http::forbidden:
        status = http_connection_type::forbidden;
        break;
    case error::http::not_found:
        status = http_connection_type::not_found;
        break;
    case error::http::method_not_allowed:
        status = http_connection_type::not_supported;
        break;
    case error::http::not_acceptable:
        status = http_connection_type::not_acceptable;
        break;
    case error::http::length_required:
        status = http_connection_type::bad_request;
        break;

    case error::http::internal_server_error:
        status = http_connection_type::internal_server_error;
        break;
    case error::http::not_implemented:
        status = http_connection_type::not_implemented;
        break;
    case error::http::bad_gateway:
        status = http_connection_type::bad_gateway;
        break;
    case error::http::service_unavailable:
        status = http_connection_type::service_unavailable;
        break;
    case error::http::insufficient_storage:
        status = http_connection_type::space_unavailable;
        break;

    default:
        return false;
    }

    return true;
}

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

struct request_properties
{
    const boost::chrono::steady_clock::time_point request_time;

    boost::optional<archiving::file_type::file_type_t> file_type;
    boost::optional<archiving::org_id_t> org_id;
    boost::optional<boost::uuids::uuid> uuid;

    boost::optional<time_t> creation_time;
    boost::optional<app::policy::id_type> policy_id;

    boost::optional<bool> expect_100_continue;
    boost::optional<size_t> content_length;
    boost::optional<std::string> content_type;
    boost::optional<std::string> pbx_hostname;
    boost::optional<std::string> content_md5;

    std::string get_pbx_hostname() const
    {
        if (pbx_hostname == boost::none) {
            return "unknown";
        }

        return *pbx_hostname;
    }

    explicit request_properties(const http_request_type& req) : request_time(boost::chrono::steady_clock::now())
    {
        try {
            if (req.method != "PUT") {
                boost::throw_exception(
                        boost::enable_error_info(error::http_error_exception())
                                << error::error_info_error_condition(
                                        boost::system::error_condition(error::http::method_not_allowed)));
            }

            http_request_type::vector_type::const_iterator front(req.headers.begin());
            const http_request_type::vector_type::const_iterator& end(req.headers.end());
            for (; front != end; ++front) {
                if (front->name == "Expect" && std::string::npos != front->value.find("100-continue")) {
                    expect_100_continue = true;
                } else if (front->name == "Content-Length") {
                    content_length = boost::lexical_cast<size_t>(front->value);
                } else if (front->name == "Content-Type") {
                    content_type = front->value;
                } else if (front->name == "Content-MD5") {
                    content_md5 = utils::base64_to_hex(front->value);
                } else if (front->name == "Accept") {
                    if (!boost::regex_search(front->value,
                                             boost::regex("(?:^|, )(?:application/json|\\*/\\*)(?=$|[;,])"))) {

                        boost::throw_exception(
                                boost::enable_error_info(error::http_error_exception("JSON not acceptable"))
                                        << error::error_info_error_condition(
                                                boost::system::error_condition(error::http::not_acceptable)));
                    }
                }
            }

            std::string path(boost::network::http::destination(req));
            std::string query_string;
            const std::string::size_type& query_pos(path.find('?'));
            if (std::string::npos != query_pos) {
                query_string = std::string(path, query_pos + 1);
                path.erase(query_pos);
            }

            if (!path.empty()) {
                std::vector<std::string> path_components;
                boost::algorithm::split(
                        path_components, path, boost::algorithm::is_any_of("/"), boost::token_compress_on);
                path_components.erase(std::remove_if(path_components.begin(), path_components.end(),
                                                     boost::bind(&std::string::empty, _1)));

                if (path_components.size() == 3) {
                    file_type = archiving::file_type::from_string(path_components[0]);
                    org_id = boost::lexical_cast<archiving::org_id_t>(path_components[1]);
                    uuid = boost::lexical_cast<boost::uuids::uuid>(path_components[2]);
                } else {
                    boost::throw_exception(
                            boost::enable_error_info(error::http_error_exception("Unexpected path-info in request"))
                                    << error::error_info_error_condition(
                                            boost::system::error_condition(error::http::bad_request)));
                }
            }

            if (!query_string.empty()) {
                {
                    const std::string::size_type& pos(query_string.find("policy="));
                    if (pos != std::string::npos && (pos == 0 || query_string.at(pos - 1) == '&')) {
                        const std::string::size_type& amp_pos(query_string.find('&', pos + 7));
                        policy_id = boost::lexical_cast<app::policy::id_type>(
                                std::string(query_string, pos + 7,
                                            amp_pos != std::string::npos ? (amp_pos - pos - 7) : std::string::npos));
                    }
                }

                {
                    const std::string::size_type& pos(query_string.find("time="));
                    if (pos != std::string::npos && (pos == 0 || query_string.at(pos - 1) == '&')) {
                        const std::string::size_type& amp_pos(query_string.find('&', pos + 5));
                        creation_time = boost::lexical_cast<time_t>(
                                std::string(query_string, pos + 5,
                                            amp_pos != std::string::npos ? (amp_pos - pos - 5) : std::string::npos));
                    }
                }

                {
                    const std::string::size_type& pos(query_string.find("pbx="));
                    if (pos != std::string::npos && (pos == 0 || query_string.at(pos - 1) == '&')) {
                        const std::string::size_type& amp_pos(query_string.find('&', pos + 4));
                        pbx_hostname = std::string(query_string, pos + 4,
                                                   amp_pos != std::string::npos ? (amp_pos - pos - 4)
                                                                                : std::string::npos);
                    }
                }
            }

            if (content_length == boost::none) {
                boost::throw_exception(
                        boost::enable_error_info(error::http_error_exception("Content-Length not given"))
                                << error::error_info_error_condition(
                                        boost::system::error_condition(error::http::length_required)));
            }

            if (content_length <= static_cast<size_t>(0)) {
                boost::throw_exception(
                        boost::enable_error_info(error::http_error_exception("Content-Length invalid"))
                                << error::error_info_error_condition(
                                        boost::system::error_condition(error::http::bad_request)));
            }

            if (content_type == boost::none || (*content_type).empty()) {
                boost::throw_exception(
                        boost::enable_error_info(error::http_error_exception("Content-Type invalid"))
                                << error::error_info_error_condition(
                                        boost::system::error_condition(error::http::bad_request)));
            }

            if (content_md5 == boost::none || (*content_md5).empty()) {
                boost::throw_exception(
                        boost::enable_error_info(error::http_error_exception("Content-MD5 invalid"))
                                << error::error_info_error_condition(
                                        boost::system::error_condition(error::http::bad_request)));
            }

            if (policy_id == boost::none) {
                boost::throw_exception(
                        boost::enable_error_info(error::http_error_exception("Policy-ID not given"))
                                << error::error_info_error_condition(
                                        boost::system::error_condition(error::http::bad_request)));
            }

            if (creation_time == boost::none) {
                boost::throw_exception(
                        boost::enable_error_info(error::http_error_exception("Creation-Time not specified"))
                                << error::error_info_error_condition(
                                        boost::system::error_condition(error::http::bad_request)));
            }
        } catch (const error::http_error_exception& e) {
            throw;
        } catch (const std::exception& e) {
            boost::throw_exception(
                    boost::enable_error_info(error::http_error_exception(e.what()))
                            << error::error_info_error_condition(
                                    boost::system::error_condition(error::http::bad_request)));
        }
    }
};


std::ostream& operator<<(std::ostream& os, const request_properties& h)
{
    os << '{';

    os << "file_type: " << h.file_type << ", ";
    os << "org_id: " << h.org_id << ", ";
    os << "uuid: " << h.uuid << ", ";

    os << "creation_time: " << h.creation_time << ", ";
    os << "policy_id: " << h.policy_id << ", ";

    os << "expect_100_continue: " << h.expect_100_continue << ", ";
    os << "content_length: " << h.content_length << ", ";
    os << "content_type: " << h.content_type << ", ";
    os << "pbx_hostname: " << h.pbx_hostname << ", ";
    os << "content_md5: " << h.content_md5;

    os << '}';

    return os;
}


struct handled_request
{
    const uint64_t id;

    const http_connection_ptr_type connection;
    const request_properties properties;

    size_t bytes_remaining;
    boost::shared_ptr<rml::utils::tmp_fstream> fstream;

    handled_request(const rml::utils::snowflake::id_t id, const http_connection_ptr_type& connection,
                    const http_request_type& request) :
            id(id), connection(connection), properties(request),
            bytes_remaining(*(properties.content_length))
    {}
};

struct archived_shim
{
#pragma clang diagnostic push
#pragma ide diagnostic ignored "TemplateArgumentsIssues"
    typedef boost::unordered_map<uint64_t, boost::shared_ptr<handled_request> > handled_request_map_type;
#pragma clang diagnostic pop

    rml::utils::snowflake& snowflake;
    const boost::shared_ptr<archivable_queue> send_queue;
    const boost::shared_ptr<response_queue> responder_queue;

    boost::mutex mutex;
    handled_request_map_type connection_map;

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

    void read_callback(
            const boost::iterator_range<const char*>& bytes_range,
            const boost::system::error_code& ec,
            const size_t& bytes_count,
            const http_connection_ptr_type& connection,
            const boost::shared_ptr<handled_request> request_state)
    try
    {
        // if there are errors; bail out and return the error to client or most likely just close the connection / just drop it off!
        if (ec || connection->has_error()) {
            LogMessage(request_state->id << " Error while reading request; ErrorCode:'" << ec << "' ConnectionError:"
                                         << RML_YES_NO(connection->has_error()));
            return;
        }

        request_state->fstream->write(bytes_range.begin(), bytes_count);
        request_state->bytes_remaining -= bytes_count;
        if (request_state->bytes_remaining > 0) {
            // If we are to handle too many requests, resulting in too many open file-handles, consider closing
            // the file-stream here, and re-opening before the next write (in append-mode)

            // if there are more to get from the request, keep reading
            connection->read(
                    boost::bind(&archived_shim::read_callback, this, _1, _2, _3, _4, request_state));
            return;
        }

        request_state->fstream->close();

        // prepare the archivable object
        boost::shared_ptr<archiving::archivable> archivable(
                new archiving::archivable(
                        boost::lexical_cast<std::string>(request_state->properties.uuid),
                        "" /* originator uuid */,
                        *(request_state->properties.creation_time),
                        request_state->fstream->get_filename(),
                        *(request_state->properties.file_type),
                        "json",
                        request_state->properties.get_pbx_hostname(),
                        *(request_state->properties.policy_id),
                        false,
                        false,
                        "" /* modified file path */,
                        *(request_state->properties.content_md5),
                        archiving::archivable::state::collected));
        archivable->set_database_id(request_state->id);

        boost::system::error_condition error;
        send_queue->produce(archivable, boost::chrono::seconds(10), error);
        if (error) {
            const bool is_busy_or_shutting_down(error.value() == queue::error::runtime_errors::is_full
                                                || error.value() == queue::error::runtime_errors::is_finishing);
            if (is_busy_or_shutting_down) {
                boost::throw_exception(
                        boost::enable_error_info(error::http_error_exception())
                                << error::error_info_error_condition(
                                        boost::system::error_condition(error::http::service_unavailable))
                                << error::error_info_internal_error_message(error.message()));
            }

            boost::throw_exception(
                    boost::enable_error_info(error::internal_error_exception())
                            << error::error_info_error_condition(error));
        }
    } catch (const error::http_error_exception& e) {
        if (std::string const* internal_error_message = boost::get_error_info<error::error_info_internal_error_message>(
                e)) {
            LogError(request_state->id << " Error: " << *internal_error_message);
        }

        boost::system::error_condition const* error_condition = boost::get_error_info<
                error::error_info_error_condition>(e);
        if (error_condition) {
            LogVerbose(request_state->id << " Error: " << *error_condition);
        }

        const std::string message(e.what());

        boost::system::error_condition error;
        responder_queue->produce(response(request_state->id, *error_condition, message), error);
        if (error) {
            LogError(request_state->id << " Failed to setup error-response; " << error);
        }
    } catch (const std::exception& e) {
        LogError(request_state->id << " Exception: " << e.what());

        if (std::string const* internal_error_message = boost::get_error_info<error::error_info_internal_error_message>(
                e)) {
            LogError(request_state->id << " Error: " << *internal_error_message);
        }

        if (boost::system::error_condition const* error_condition =
                boost::get_error_info<error::error_info_error_condition>(e)) {
            LogError(request_state->id << " Error: " << *error_condition);
        }

        boost::system::error_condition error;
        responder_queue->produce(
                response(request_state->id, boost::system::error_condition(error::http::internal_server_error)),
                error);
        if (error) {
            LogError(request_state->id << " Failed to setup error-response; " << error);
        }
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
                    response(item->get_database_id(), boost::system::error_condition(
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
                response res;
                boost::system::error_condition error;
                responder_queue->consume(res, error);
                if (!error) {
                    LogMessage(res.id << " Result: " << res.upshot << " Message: " << res.message);
                    finalise_request(res.id, res.upshot, res.message);
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
    void finalise_request(const rml::utils::snowflake::id_t id, const boost::system::error_condition& ec,
                          const std::string& message)
    {
        if (ec.category() != error::get_http_category()) {
            LogError(id << ' ' << error::http::internal_server_error << " Internal Error: " << message
                        << " (" << ec << ')');
            finalise_request(id, error::http::internal_server_error, (message.empty() ? "Internal Error" : message));
        } else {
            finalise_request(id, static_cast<error::http::enum_t>(ec.value()), message);
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
    void finalise_request(const rml::utils::snowflake::id_t id, const error::http::enum_t status_code,
                          const std::string& message)
    {
        send_response(id, status_code, render_response(status_code, message), true);
    }

    inline
    void send_response(const rml::utils::snowflake::id_t id, const error::http::enum_t status_code,
                       const std::string& response_body, const bool finish_request)
    {
        bool finish_request_(finish_request);

        handled_request_map_type::iterator itr = connection_map.find(id);
        if (itr == connection_map.end()) {
            LogError("Unable to find request instance in map; ID:" << id);
            return;
        }

        boost::shared_ptr<handled_request> request_state = itr->second;
        if (!send_response(request_state->connection, status_code, response_body)) {
            finish_request_ = true;
        }

        if (finish_request_) {
            connection_map.erase(itr);
        }
    }

    inline
    bool respond(const http_connection_ptr_type& connection, const error::http::enum_t status_code,
                 const std::string& message)
    {
        return send_response(connection, status_code, render_response(status_code, message));
    }

    inline
    bool send_response(const http_connection_ptr_type connection, const error::http::enum_t status_code,
                       const std::string& response_body)
    {
        if (status_code == error::http::continue_) {
            boost::system::error_code ec;
            connection->socket().write_some(boost::asio::buffer("HTTP/1.1 100 Continue\r\n\r\n"), ec);
            return !ec;
        }

//        http_response_header_type response_headers[] = {
//                {"Connection",   "close"}
//                {"Content-Type", "application/json"}
//        };

        boost::container::slist<http_response_header_type> headers;
        {
            http_response_header_type connection = {"Connection", "close"};
            headers.push_front(connection);
        }

        http_status_type status;
        if (to_netlib_status(status_code, status)) {
            connection->set_status(status);

            if (!response_body.empty()) {
                http_response_header_type content_type = {"Content-Type", "application/json"};
                headers.push_front(content_type);\
                http_response_header_type content_length = {"Content-Length",
                                                            boost::lexical_cast<std::string>(response_body.length())};
                headers.push_front(content_length);
            }

            connection->set_headers(boost::make_iterator_range(headers.begin(), headers.end()));

            if (!response_body.empty()) {
                connection->write(response_body);
            }

            return true;
        }

        LogError("Failed to translate response-code");

        connection->set_status(http_connection_type::internal_server_error);
        const std::string body("Internal Server Error");
        {
            http_response_header_type content_type = {"Content-Type", "text/plain"};
            headers.push_front(content_type);
            http_response_header_type content_length = {"Content-Length",
                                                        boost::lexical_cast<std::string>(body.length())};
            headers.push_front(content_length);
        }
        connection->set_headers(boost::make_iterator_range(headers.begin(), headers.end()));
        connection->write(body);

        return false;
    }

    void operator()(http_request_type const& request, http_connection_ptr_type connection)
    try
    {
        if (shutting_down == true) {
            respond(connection, error::http::service_unavailable, "Service Unavailable");
            return;
        }

        // Make a snowflake-id
        const rml::utils::snowflake::id_t& id(snowflake.make_id());

        // read the data from request and save to file
        boost::shared_ptr<handled_request> request_state = boost::make_shared<handled_request>(id, connection, request);
        if (request_state->properties.content_length != boost::none
            && request_state->properties.content_length > static_cast<size_t>(0)) {

            const std::string& path(
                    boost::str(boost::format("%1%_%2%_%3%") %
                               archiving::file_type::to_string(*(request_state->properties.file_type)) %
                               request_state->properties.org_id % request_state->properties.uuid));
            try {
                request_state->fstream = rml::utils::tmp_fstream::create(path, request_state->bytes_remaining, "/tmp");
            } catch (const rml::utils::tmp_fstream::error& e) {
                if (std::strcmp("Failed to reserve space", e.what()) == 0) {
                    respond(connection, error::http::insufficient_storage, e.what());
                } else {
                    LogError(id << ' ' << e.what());
                    respond(connection, error::http::internal_server_error, "Internal error");
                    return;
                }
            }

            if (request_state->properties.expect_100_continue == true) {
                if (!send_response(connection, error::http::continue_, "")) {
                    return;
                }
            }

            // save the connection object so that we can write back a response
            // put connection into a map[snowflake-id, connection_state<connection_ptr>]
            {
                boost::unique_lock<boost::mutex> lock(mutex);
                connection_map.insert(std::make_pair(id, request_state));
            }

            connection->read(
                    boost::bind(&archived_shim::read_callback, this, _1, _2, _3, _4, request_state));
        }
    } catch (const std::exception& e) {
        LogError("Error while accepting a request; Error: " << e.what());
        respond(connection, error::http::internal_server_error, "Internal error");
    }
};

} // namespace app

#endif //ARCHIVING_ESTUARY_APP_ARCHIVED_SHIM_HPP
