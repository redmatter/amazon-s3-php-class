//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 30/09/2020.
//

#ifndef ARCHIVED_NET_PUT_EXCEPTION_HPP
#define ARCHIVED_NET_PUT_EXCEPTION_HPP

#include <ctime>
#include <stdexcept>

namespace archiving {
namespace archived {
namespace net {
namespace put {
namespace exception {

struct bad_protocol : public std::runtime_error
{
    explicit bad_protocol(const std::string& message) : std::runtime_error(message) {}
};

struct bad_request : public std::runtime_error
{
    explicit bad_request(const std::string& message) : std::runtime_error(message) {}
};

struct internal_error : public std::runtime_error
{
    explicit internal_error(const std::string& message) : std::runtime_error(message) {}
};

struct server_busy : public std::runtime_error
{
    std::time_t until;
    server_busy(std::time_t until, const std::string& message) : std::runtime_error(message), until(until) {}
};

} // namespace exception
} // namespace put
} // namespace net
} // namespace archived
} // namespace archiving

#endif // ARCHIVED_NET_PUT_EXCEPTION_HPP
