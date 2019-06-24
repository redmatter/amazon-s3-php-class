//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 20/06/2019.
//

#ifndef ARCHIVING_CONNECTION_MANAGER_HPP
#define ARCHIVING_CONNECTION_MANAGER_HPP

#include "connection_pool.hpp"
#include "../ip_addr_utils.hpp"
#include "detail/ip_pool_map.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/shared_ptr.hpp>

namespace common
{
namespace net
{
namespace archived
{

namespace detail
{

static void
shutdown_pool(uint32_t, const boost::shared_ptr<connection_pool>& p)
{
    p->shutdown();
}

boost::shared_ptr<connection_pool>
make_connection_pool(
        boost::asio::io_service& io_service, const std::string& archived_port,
        std::size_t pool_min_size, std::size_t pool_max_size,
        uint32_t addr, boost::system::error_code& ec)
{
    ip::address ip_addr = utils::uint32_to_ip(addr, ec);
    if (ec) {
        return boost::shared_ptr<connection_pool>();
    }

    return boost::make_shared<connection_pool>(
            io_service,
            ip_addr, boost::lexical_cast<unsigned short>(archived_port),
            pool_min_size, pool_max_size);
}

} // namespace detail

namespace ip = boost::asio::ip;

class connection_manager : public boost::enable_shared_from_this<connection_manager>
{
    typedef detail::ip_pool_map<boost::shared_ptr<connection_pool>, 8> map_type;

    // timer using boost.chrono
    typedef boost::asio::basic_waitable_timer<boost::chrono::steady_clock> timer_type;

    map_type pool_map;

    const std::string archived_host;
    const std::string archived_port;
    const std::size_t pool_min_size;
    const std::size_t pool_max_size;

    boost::asio::io_service& io_service;
    ip::tcp::resolver resolver;

    boost::atomic_uint32_t active_ip_address;

    uint32_t
    get_active_ip_address()
    {
        uint32_t ip;
        while (true) {
            ip = active_ip_address.load();
            if (ip != 0) {
                break;
            }

            boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
        }

        return ip;
    }

    void
    update_active_ip_address(boost::system::error_code& ec)
    {
        ip::tcp::resolver::query query(archived_host, archived_port);
        ip::tcp::resolver::iterator endpoint_itr = resolver.resolve(query, ec);
        if (ec) {
            return;
        }

        if (endpoint_itr == ip::tcp::resolver::iterator()) {
            ec = error::make_error_code(error::cannot_resolve);
            return;
        }

        uint32_t new_result = utils::ip_to_uint32(endpoint_itr->endpoint().address(), ec);
        if (ec) {
            return;
        }

        active_ip_address.store(new_result, boost::memory_order_acquire);
    }

    void
    scheduled_update_active_ip_address(const boost::system::error_code& ec, const boost::shared_ptr<timer_type>& timer)
    {
        // timer was cancelled (during shutdown)
        if (ec == boost::asio::error::operation_aborted) {
            return;
        }

        boost::system::error_code err;
        update_active_ip_address(err);

        boost::chrono::seconds delay(10);

        // if there was an error, try again in 2s
        if (err) {
            LogWarning("failed to update active ip address; retrying shortly");
            delay = boost::chrono::seconds(2);
        }

        timer->expires_from_now(delay, err);
        if (err) {
            LogError("failed to schedule active ip address update");
            return;
        }

        timer->async_wait(
                boost::bind(&connection_manager::scheduled_update_active_ip_address, shared_from_this(), _1, timer));
    }

    static void
    send_nop_to_pool(uint32_t, boost::shared_ptr<connection_pool>& pool)
    {
        pool->send_nop_to_connections();
    }

    void
    scheduled_nop_to_connections(const boost::system::error_code& ec, const boost::shared_ptr<timer_type>& timer)
    {
        pool_map.for_each(boost::bind(connection_manager::send_nop_to_pool, _1, _2));
    }

    boost::shared_ptr<connection_pool>
    get_connection_pool(uint32_t addr, boost::system::error_code& ec)
    {
        if (addr == 0) {
            addr = get_active_ip_address();
        }

        return pool_map.get_pool(addr, ec);
    }

public:
    connection_manager(boost::asio::io_service& io_service,
                       const std::string& archived_host, const std::string& archived_port,
                       std::size_t pool_min_size, std::size_t pool_max_size) :
            pool_map(boost::bind(detail::make_connection_pool, boost::ref(io_service), archived_port,
                                 pool_min_size, pool_max_size, _1, _2)),
            archived_host(archived_host), archived_port(archived_port),
            pool_min_size(pool_min_size), pool_max_size(pool_max_size),
            io_service(io_service), resolver(io_service),
            active_ip_address(0)
    {
        io_service.post(boost::bind(
                &connection_manager::scheduled_update_active_ip_address, shared_from_this(),
                boost::system::error_code(), boost::make_shared<timer_type>(io_service)));
        io_service.post(boost::bind(
                &connection_manager::scheduled_nop_to_connections, shared_from_this(),
                boost::system::error_code(), boost::make_shared<timer_type>(io_service)));
    }

    boost::shared_ptr<connection>
    get_connection(uint32_t addr, boost::system::error_code& ec)
    {
        boost::shared_ptr<connection_pool> pool = get_connection_pool(addr, ec);
        if (ec) {
            return boost::shared_ptr<connection>();
        }

        return pool->grab_wait(ec);
    }

    void shutdown()
    {
        pool_map.for_each(boost::bind(detail::shutdown_pool, _1, _2));
    }
};

} // namespace archived
} // namespace net
} // namespace common

#endif //ARCHIVING_CONNECTION_MANAGER_HPP
