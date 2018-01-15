//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 10/01/2018.
//

#ifndef ARCHIVING_ESTUARY_APP_HANDLED_REQUEST_HPP
#define ARCHIVING_ESTUARY_APP_HANDLED_REQUEST_HPP

#include "http/server.hpp"
#include "http/request.hpp"
#include "http/correlation.hpp"

namespace app
{
struct handled_request
{
    const boost::chrono::steady_clock::time_point time;
    const http::request request;
    const http::correlation correlation;

    handled_request(const uint64_t id, const http::server::request& req,
                     const boost::shared_ptr<http::server::connection>& connection) :
            time(boost::chrono::steady_clock::now()), request(req, connection),
            correlation(id, request.get_first_header_value<std::string>("X-Request-ID"),
                        request.get_first_header_value_as<std::string>("X-Correlation-ID"))
    {
    }
};
} // namespace app

#endif //ARCHIVING_ESTUARY_APP_HANDLED_REQUEST_HPP
