//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 11/01/2018.
//

#ifndef ARCHIVING_PENDING_REQUEST_HPP
#define ARCHIVING_PENDING_REQUEST_HPP

#include "request_wrapped.hpp"
#include "request_correlation.hpp"
#include "request_progress.hpp"

#include <rml/statistics/progress/marker.hpp>

#include <boost/chrono/chrono.hpp>
#include <boost/any.hpp>

#include <string>

#include <stdint.h>

namespace app
{
namespace http
{
struct pending_request
{
    typedef rml::statistics::progress::marker<request_progress::enum_t> progress_marker_type;

    const boost::chrono::steady_clock::time_point time;
    const http::request_wrapped request;
    const http::request_correlation correlation;

    mutable boost::any data;
    mutable progress_marker_type progress_marker;

    pending_request(const uint64_t id, const server::request& req,
                    const boost::shared_ptr<server::connection>& connection) :
            time(boost::chrono::steady_clock::now()), request(connection, req),
            correlation(id, request.get_first_header_value_as<std::string>("X-Request-ID"),
                        request.get_first_header_value_as<std::string>("X-Correlation-ID"))
    {
        mark_progress(request_progress::received);
    }

    void mark_progress(request_progress::enum_t progress_made) const
    {
        progress_marker.add_mark(progress_made);
    }

    template<typename T>
    boost::shared_ptr<const T> make_shared_data_from_request() const
    {
        try {
            boost::shared_ptr<const T> d = boost::make_shared<T>(*this,
                    boost::bind(&progress_marker_type::add_mark, boost::ref(progress_marker), _1));
            data = d;
            return d;
        } catch (const error::http_error_exception& e) {
            throw;
        } catch (const error::bad_data_exception& e) {
            boost::throw_exception(
                    boost::enable_error_info(error::http_error_exception(e.what()))
                            << error::error_info_request_correlation(correlation)
                            << error::error_info_error_condition(
                                    boost::system::error_condition(error::http::bad_request)));
        } catch (const boost::bad_lexical_cast& e) {
            boost::throw_exception(
                    boost::enable_error_info(error::http_error_exception())
                            << error::error_info_request_correlation(correlation)
                            << error::error_info_error_condition(
                                    boost::system::error_condition(error::http::bad_request)));
        } catch (const std::exception& e) {
            boost::throw_exception(
                    boost::enable_error_info(error::http_error_exception(e.what()))
                            << error::error_info_request_correlation(correlation)
                            << error::error_info_error_condition(
                                    boost::system::error_condition(error::http::internal_server_error)));
        }
    }

    template<typename T>
    boost::shared_ptr<const T> get_data() const
    {
        return boost::any_cast<boost::shared_ptr<const T> >(data);
    }

    inline
    bool respond(const error::http::enum_t status_code,
                 const std::string& content_type = "application/json",
                 const std::string& body = "",
                 const response_header_list& headers = response_header_list()) const
    {
        response_header_list response_headers = headers;
        response_headers.add(
                    "RM-Server-ID",
                    boost::str(boost::format("%1%")
                               % boost::io::group(std::hex, std::setw(16), std::setfill('0'), correlation.id)));
        if (correlation.request_id != boost::none) {
            response_headers.add("X-Request-ID", *(correlation.request_id));
        }

        if (correlation.correlation_id != boost::none) {
            response_headers.add("X-Correlation-ID", *(correlation.correlation_id));
        }

        mark_progress(request_progress::response_sent);

        return request.respond(status_code, content_type, body, response_headers);
    }
};
} // namespace http
} // namespace app
#endif //ARCHIVING_PENDING_REQUEST_HPP
