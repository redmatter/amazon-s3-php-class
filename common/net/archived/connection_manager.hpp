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
#include <boost/system/error_code.hpp>

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
            boost::ref(io_service),
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

    bool shutting_down;

    void
    update_active_ip_address(boost::system::error_code& ec)
    {
        LogVerbose("Updating ArchiveD IP");

        ip::tcp::resolver::query query(archived_host, archived_port);
        ip::tcp::resolver::iterator endpoint_itr = resolver.resolve(query, ec);
        if (ec) {
            return;
        }

        if (endpoint_itr == ip::tcp::resolver::iterator()) {
            ec = error::make_error_code(error::cannot_resolve);
            return;
        }

        const ip::address& ip_addr(endpoint_itr->endpoint().address());
        uint32_t ip_int32 = utils::ip_to_uint32(ip_addr, ec);
        if (ec) {
            return;
        }

        active_ip_address.store(ip_int32, boost::memory_order_acquire);

        LogMessage("Current ArchiveD IP: " << ip_addr);
    }

    void
    scheduled_update_active_ip_address(const boost::system::error_code& ec, const boost::shared_ptr<timer_type>& timer)
    {
        // timer was cancelled (during shutdown)
        if (ec == boost::asio::error::operation_aborted || shutting_down) {
            LogVerbose("ArchiveD IP updater shutting down");
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
                boost::bind(&connection_manager::scheduled_update_active_ip_address, shared_from_this(),
                        boost::asio::placeholders::error, timer));
    }

    static void
    send_nop_to_pool(uint32_t ip, boost::shared_ptr<connection_pool>& pool)
    {
        boost::system::error_code ec;
        LogMessage(utils::uint32_to_ip(ip, ec) << " sending NOPs to inactive connections in pool");
        pool->send_nop_to_connections();
    }

    void
    scheduled_nop_to_connections(const boost::system::error_code& ec, const boost::shared_ptr<timer_type>& timer)
    {
        // timer was cancelled (during shutdown)
        if (ec == boost::asio::error::operation_aborted || shutting_down) {
            LogVerbose("NOP-er shutting down");
            return;
        }

        pool_map.for_each(
                boost::bind(connection_manager::send_nop_to_pool, _1, _2));

        boost::system::error_code err;
        timer->expires_from_now(boost::chrono::seconds(10), err);
        if (err) {
            LogError("failed to schedule NOPs for inactive connections");
            return;
        }

        timer->async_wait(
                boost::bind(&connection_manager::scheduled_nop_to_connections, shared_from_this(),
                            boost::asio::placeholders::error, timer));
    }

    boost::shared_ptr<connection_pool>
    get_connection_pool(uint32_t addr, boost::system::error_code& ec)
    {
        if (addr == 0) {
            // TODO .. any error would do here, but any confusion should be avoided
            ec = boost::system::posix::make_error_code(boost::system::posix::invalid_argument);
            return boost::shared_ptr<connection_pool>();
        }

        return pool_map.get_pool(addr, ec);
    }

public:
    static void describe_config(boost::program_options::options_description& optsDesc)
    {
        optsDesc.add_options()
                ("archived_client.server_port",
                 boost::program_options::value<std::string>()->default_value(ARCHIVED_LISTEN_PORT),
                 "ArchiveD listen port")
                ("archived_client.server_host",
                 boost::program_options::value<std::string>()->default_value("archived.redmatter.com"),
                 "ArchiveD host name")
                ("archived_connection_pool.max_connections",
                 boost::program_options::value<std::size_t>()->default_value(4),
                 "max number of connections to ArchiveD");
    }

    connection_manager(boost::asio::io_service& io_service,
                       const std::string& archived_host, const std::string& archived_port,
                       std::size_t pool_min_size, std::size_t pool_max_size) :
            pool_map(boost::bind(detail::make_connection_pool, boost::ref(io_service), archived_port,
                                 pool_min_size, pool_max_size, _1, _2)),
            archived_host(archived_host), archived_port(archived_port),
            pool_min_size(pool_min_size), pool_max_size(pool_max_size),
            io_service(io_service), resolver(io_service),
            active_ip_address(0), shutting_down(false)
    {}

    template<class Rep, class Period>
    uint32_t
    get_active_ip_address(boost::system::error_code& ec,
                          const boost::chrono::duration<Rep, Period>& timeout = boost::chrono::duration<Rep, Period>())
    {
        const boost::chrono::steady_clock::time_point start_time(boost::chrono::steady_clock::now());

        uint32_t ip;
        while (true) {
            ip = active_ip_address.load();
            if (ip != 0) {
                break;
            }

            // if no timeout given (i.e zero value)
            if (timeout.count() != 0 
                && start_time + timeout < boost::chrono::steady_clock::now()) {

                ec.assign(boost::system::errc::timed_out, boost::system::get_system_category());
                break;
            }

            boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
        }

        return ip;
    }

    /**
     * @brief Get the connection object
     * 
     * @param addr 
     * @param ec 
     * @return boost::shared_ptr<connection> 
     */
    boost::shared_ptr<connection>
    get_connection(uint32_t addr, boost::system::error_code& ec)
    {
        boost::shared_ptr<connection_pool> pool = get_connection_pool(addr, ec);
        if (ec) {
            return boost::shared_ptr<connection>();
        }

        return pool->grab_wait(ec);
    }

    /**
     * @brief Get the pool level; how full are we?
     * 
     * @param ec 
     * @return float 
     */
    float
    get_pool_level(boost::system::error_code& ec)
    {
        uint32_t archived_ip = get_active_ip_address(ec, boost::chrono::seconds(1));
        if (ec) {
            return 0.0;
        }

        boost::shared_ptr<connection_pool> pool = get_connection_pool(archived_ip, ec);
        if (ec) {
            return 0.0;
        }

        float max_size = static_cast<float>(pool->max_size());
        if (max_size < 1) {
            return 0.0;
        }

        float size = static_cast<float>(
            std::max<std::size_t>(pool->size(), 1));

        return size / max_size;
    }

    void
    start()
    {
        io_service.post(boost::bind(
                &connection_manager::scheduled_update_active_ip_address, shared_from_this(),
                boost::system::error_code(), boost::make_shared<timer_type>(boost::ref(io_service))));
        io_service.post(boost::bind(
                &connection_manager::scheduled_nop_to_connections, shared_from_this(),
                boost::system::error_code(), boost::make_shared<timer_type>(boost::ref(io_service))));
    }

    void shutdown()
    {
        shutting_down = true;
        pool_map.for_each(boost::bind(detail::shutdown_pool, _1, _2));
    }
};

} // namespace archived
} // namespace net
} // namespace common

#endif //ARCHIVING_CONNECTION_MANAGER_HPP
