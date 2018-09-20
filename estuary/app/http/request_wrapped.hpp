//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 10/01/2018.
//

#ifndef ARCHIVING_ESTUARY_APP_HTTP_REQUEST_HPP
#define ARCHIVING_ESTUARY_APP_HTTP_REQUEST_HPP

#include "server.hpp"
#include "detail/path_info.hpp"
#include "detail/query_map.hpp"

namespace app
{
namespace http
{

struct request_wrapped
{
    mutable boost::shared_ptr<server::connection> connection;
    const server::request& req;

    mutable bool query_map_;
    mutable detail::query_map query_map;

    mutable bool path_vector_;
    mutable detail::path_info path_vector;

    request_wrapped(const boost::shared_ptr<server::connection>& connection, const server::request& req) :
            connection(connection), req(req), query_map_(false), path_vector_(false)
    {}

    inline
    std::string get_method() const
    {
        return req.method;
    }

    inline
    std::string get_source() const
    {
        return boost::network::http::source(req);
    }

    inline
    std::string get_destination() const
    {
        return boost::network::http::destination(req);
    }

    inline
    std::string get_path() const
    {
        const std::string& destination(get_destination());
        return std::string(destination, 0, destination.find('?'));
    }

    inline std::string get_query() const
    {
        const std::string& destination(get_destination());
        const std::string::size_type& query_pos(destination.find('?'));
        if (query_pos == std::string::npos) {
            return "";
        }

        return std::string(destination, query_pos + 1);
    }

    const detail::path_info& get_path_vector() const
    {
        if (!path_vector_) {
            path_vector = detail::path_info(get_path());
            path_vector_ = true;
        }

        return path_vector;
    }

    const detail::query_map& get_query_map() const
    {
        if (!query_map_) {
            const boost::network::uri::uri uri("http://ignoreme" + std::string(boost::network::http::destination(req)));
            if (uri.is_valid()) {
                boost::network::uri::query_map(uri, query_map);
            } else {
                LogError("Invalid URI " << std::string(boost::network::http::destination(req)));
            }
            query_map_ = true;
        }

        return query_map;
    }

    const server::request::headers_container_type& get_headers() const
    {
        return req.headers;
    }

    std::string get_first_header_value(const std::string& header, bool& found) const
    {
        server::request::headers_container_type::const_iterator front(req.headers.begin());
        const server::request::headers_container_type::const_iterator& end(req.headers.end());
        for (; front != end; ++front) {
            if (front->name == header) {
                found = true;
                return front->value;
            }
        }

        found = false;
        return std::string();
    }

    template<typename T>
    boost::optional<T> get_first_header_value_as(const std::string& header) const
    {
        bool found(false);
        const std::string& value_str(get_first_header_value(header, found));

        boost::optional<T> value;
        if (found) {
            value = boost::lexical_cast<T>(value_str);
        }

        return value;
    }

    bool expect_100_continue() const
    {
        bool found(false);
        const std::string& expect(get_first_header_value("Expect", found));
        return found && std::string::npos != expect.find("100-continue");
    }

    inline
    bool respond(const error::http::enum_t status_code,
                 const std::string& content_type = "application/json",
                 const std::string& body = "",
                 const response_header_list& headers = response_header_list()) const
    {
        return send_response(connection, status_code, content_type, body, headers);
    }
};
} // namespace http
} // namespace app

#endif //ARCHIVING_ESTUARY_APP_HTTP_REQUEST_HPP
