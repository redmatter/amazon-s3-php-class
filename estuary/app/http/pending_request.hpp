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
#include "correlation.hpp"

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
    const boost::chrono::steady_clock::time_point time;
    const http::request_wrapped request;
    const http::correlation correlation;

    mutable boost::any data;

    pending_request(const uint64_t id, const server::request& req,
                    const boost::shared_ptr<server::connection>& connection) :
            time(boost::chrono::steady_clock::now()), request(connection, req),
            correlation(id, request.get_first_header_value_as<std::string>("X-Request-ID"),
                        request.get_first_header_value_as<std::string>("X-Correlation-ID"))
    {
    }


    template<typename T>
    boost::shared_ptr<const T> make_shared_data_from_request() const
    {
        boost::shared_ptr<const T> d = boost::make_shared<T>(*this);
        data = d;
        return d;
    }

    template<typename T>
    T get_data() const
    {
        return boost::any_cast<T>(data);
    }
};
} // namespace http
} // namespace app
#endif //ARCHIVING_PENDING_REQUEST_HPP
