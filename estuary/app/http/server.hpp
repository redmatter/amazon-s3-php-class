//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 10/01/2018.
//

#ifndef ARCHIVING_ESTUARY_APP_HTTP_SERVER_HPP
#define ARCHIVING_ESTUARY_APP_HTTP_SERVER_HPP

#include <boost/container/slist.hpp>
#include <boost/network/protocol/http/server.hpp>

#include "../error.hpp"
#include "../../defines.h"

namespace app
{

struct archived_shim;

namespace http
{

typedef boost::network::http::async_server<archived_shim> server;
typedef server::connection connection_type;
typedef connection_type::status_t status_type;
typedef server::string_type string_type;
typedef server::response_header response_header_type;

struct response_header_list : public boost::container::slist<response_header_type>
{
    response_header_list& add(const string_type& name, const string_type& value)
    {
        response_header_type h = {name, value};
        push_front(h);

        return *this;
    }
};

bool to_netlib_status(const error::http::enum_t e, status_type& status)
{
    switch (e) {
    case error::http::ok:
        status = connection_type::ok;
        break;
    case error::http::created:
        status = connection_type::created;
        break;
    case error::http::accepted:
        status = connection_type::accepted;
        break;
    case error::http::no_content:
        status = connection_type::no_content;
        break;

    case error::http::multiple_choices:
        status = connection_type::multiple_choices;
        break;
    case error::http::moved_permanently:
        status = connection_type::moved_permanently;
        break;
    case error::http::moved_temporarily:
        status = connection_type::moved_temporarily;
        break;
    case error::http::not_modified:
        status = connection_type::not_modified;
        break;

    case error::http::bad_request:
        status = connection_type::bad_request;
        break;
    case error::http::unauthorized:
        status = connection_type::unauthorized;
        break;
    case error::http::forbidden:
        status = connection_type::forbidden;
        break;
    case error::http::not_found:
        status = connection_type::not_found;
        break;
    case error::http::method_not_allowed:
        status = connection_type::not_supported;
        break;
    case error::http::not_acceptable:
        status = connection_type::not_acceptable;
        break;
    case error::http::length_required:
        status = connection_type::bad_request;
        break;

    case error::http::internal_server_error:
        status = connection_type::internal_server_error;
        break;
    case error::http::not_implemented:
        status = connection_type::not_implemented;
        break;
    case error::http::bad_gateway:
        status = connection_type::bad_gateway;
        break;
    case error::http::service_unavailable:
        status = connection_type::service_unavailable;
        break;
    case error::http::insufficient_storage:
        status = connection_type::space_unavailable;
        break;

    default:
        return false;
    }

    return true;
}

bool send_response(const boost::shared_ptr<server::connection>& connection,
                   const error::http::enum_t status_code,
                   const std::string& content_type,
                   const std::string& body,
                   const response_header_list& header_list = response_header_list())
{
    if (status_code == error::http::continue_) {
        boost::system::error_code ec;
        connection->socket().write_some(boost::asio::buffer("HTTP/1.1 100 Continue\r\n\r\n"), ec);
        return !ec;
    }

    // Here we assume that the client code does not add any of "Connection", "Content-Type" or "Content-Length" headers
    // into the header_list. We can improve the code here by adding a "filter_iterator + range".
    // http://www.boost.org/doc/libs/1_54_0/libs/iterator/doc/filter_iterator.html

    response_header_list headers = header_list;
    headers.add("Server", DEFAULT_APP_LONG_NAME);
    headers.add("Connection", "close");

    server::connection::status_t status;
    if (!to_netlib_status(status_code, status)) {
        return false;
    }

    connection->set_status(status);
    if (error::http::has_response_body(status_code)) {
        if (content_type.empty()) {
            headers.add("Content-Type", "text/plain");
        } else {
            headers.add("Content-Type", content_type);
        }
        headers.add("Content-Length", boost::lexical_cast<std::string>(body.length()));

        connection->set_headers(boost::make_iterator_range(headers.begin(), headers.end()));
        connection->write(body);
    } else {
        connection->set_headers(boost::make_iterator_range(headers.begin(), headers.end()));
        connection->write("");
    }

    return true;
}

} // namespace http
} // namespace app

#endif //ARCHIVING_ESTUARY_APP_HTTP_SERVER_HPP
