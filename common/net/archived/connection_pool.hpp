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
#include "send_nop.hpp"

#include <rml/pool/pools.hpp>

#include <boost/chrono/system_clocks.hpp>

namespace common
{
namespace net
{
namespace archived
{

namespace detail
{
bool check_last_leased_time(const boost::chrono::steady_clock::time_point& lease_time)
{
    return (lease_time + boost::chrono::seconds(10)) < boost::chrono::steady_clock::now();
}
} // namespace detail

class connection_pool : public rml::pool::locking_lifo_pool<connection>
{
    typedef rml::pool::locking_lifo_pool<connection> base_pool_type;

    static bool
    check_grabbed(
            const boost::shared_ptr<connection>& conn,
            const boost::chrono::steady_clock::time_point& idle_since)
    {
        return conn->is_ok();
    }

    static connection*
    create_connection(boost::asio::io_service& ios, const ip::address& addr, unsigned short port)
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

    bool send_nop_per_connection(boost::shared_ptr<connection> c)
    {
        bool ok = c->is_ok();

        // see if we should keep this connection. if it has not been used to send a file for 2 minutes, it can be
        // closed.
        if (ok && c->no_puts_for(boost::chrono::minutes(2))) {
            ok = false;
        }

        if (ok) {
            boost::system::error_code ec;
            send_nop(c, ec);

            ok = ec;
        }

        if (!ok) {
            base_pool_type::release_faulty(c);
        }

        return true;
    }

    bool shutting_down;

    bool is_not_shutting_down() const
    {
        return shutting_down;
    }

public:
    connection_pool(boost::asio::io_service& ios, const ip::address& addr, unsigned short port,
                    const std::size_t min_size, const std::size_t max_size) :
            base_pool_type(boost::lexical_cast<std::string>(addr), min_size, max_size,
                           boost::bind(create_connection, boost::ref(ios), boost::cref(addr), port),
                           boost::bind(check_grabbed, _1, _2)),
            shutting_down(false)
    {}

    void send_nop_to_connections()
    {
        boost::system::error_code ec;
        unsigned noped = apply_all_if(
                boost::bind(detail::check_last_leased_time, _1),
                boost::bind(&connection_pool::send_nop_per_connection, this, _1),
                boost::bind(&connection_pool::is_not_shutting_down, this),
                ec);

        if (ec) {
            LogWarning("Error during NOPs; error:" << ec.message());
        }

        LogMessage("Sent NOP to " << noped << " connections");
    }

    void shutdown()
    {
        shutting_down = true;
        base_pool_type::shutdown();
    }
};

} // namespace archived
} // namespace net
} // namespace common

#endif //ARCHIVING_COMMON_NET_ARCHIVED_CONNECTION_POOL_HPP
