//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 20/06/2019.
//

#ifndef ARCHIVING_COMMON_NET_ARCHIVED_CONNECTION_POOL_HPP
#define ARCHIVING_COMMON_NET_ARCHIVED_CONNECTION_POOL_HPP

#include "connection.hpp"

#include <rml/pool/pools.hpp>

#include <boost/chrono/system_clocks.hpp>

namespace common
{
namespace net
{
namespace archived
{

class connection_pool : public rml::pool::locking_lifo_pool<connection>
{
    typedef rml::pool::locking_lifo_pool<connection> base_pool_type;

    static bool check_grabbed(
            const boost::shared_ptr<connection>& conn,
            const boost::chrono::steady_clock::time_point& idle_since)
    {
        return conn->is_ok();
    }

    static connection* create_connection(boost::asio::io_service& ios, const ip::address& addr, unsigned short port)
    {
        connection* c = new connection(ios, addr, port);
        try {
            boost::system::error_code ec;
            c->start(ec);

            if (ec) {
                throw std::runtime_error(ec.message());
            }

            return c;
        } catch (...) {
            delete c;
            throw;
        }
    }

public:
    connection_pool(boost::asio::io_service& ios, const ip::address& addr, unsigned short port,
                    const std::size_t min_size, const std::size_t max_size) :
            base_pool_type(boost::lexical_cast<std::string>(addr), min_size, max_size,
                           boost::bind(create_connection, boost::ref(ios), boost::cref(addr), port),
                           boost::bind(check_grabbed, _1, _2))
    {}
};

} // namespace archived
} // namespace net
} // namespace common

#endif //ARCHIVING_COMMON_NET_ARCHIVED_CONNECTION_POOL_HPP
