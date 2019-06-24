//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 20/06/2019.
//

#ifndef ARCHIVING_COMMON_NET_ARCHIVED_CONNECTION_HPP
#define ARCHIVING_COMMON_NET_ARCHIVED_CONNECTION_HPP

#include "error.hpp"
#include "../../defines.h"

#include <rml/version.h>
#include <rml/logger.hpp>

#include <boost/asio/streambuf.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/chrono/system_clocks.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/regex.hpp>
#include <boost/asio/write.hpp>
#include <boost/format.hpp>

namespace common
{
namespace net
{
namespace archived
{

namespace ip = boost::asio::ip;

struct connection;
std::ostream& operator<<(std::ostream&, const connection&);

struct connection
{
    const ip::address addr;
    unsigned short port;

    // for logging / debug, composed of local-port and IP address
    std::string id;

    boost::asio::streambuf recv_buffer;
    boost::asio::streambuf send_buffer;

    boost::asio::io_service& io_service;
    boost::asio::strand strand;
    boost::asio::deadline_timer timer; // todo timers
    ip::tcp::socket socket;

    boost::system::error_code fault;
    boost::chrono::steady_clock::time_point last_send_file_time;

    connection(boost::asio::io_service& io_service, const ip::address& addr, unsigned short port) :
            addr(addr), port(port),
            id("<not-connected>"),
            io_service(io_service), strand(io_service), timer(io_service), socket(io_service)
    {}

    bool is_ok() const
    {
        return socket.is_open();
    }

    template<class Rep, class Period>
    bool no_puts_for(const boost::chrono::duration<Rep, Period>& duration) const
    {
        return (last_send_file_time + duration) < boost::chrono::steady_clock::now();
    }

    bool is_faulty() const
    {
        return fault;
    }

    /**
     * start-up and handshake to ArchiveD
     *
     * @param ec
     */
    void start(boost::system::error_code& ec)
    {
        ip::tcp::endpoint ep(addr, port);
        LogVerbose(*this << " Connecting to " << ep);
        socket.connect(ep, ec);
        if (ec) {
            return;
        }

        boost::asio::socket_base::send_buffer_size send_buffer_size_option(8 * 1024);
        socket.set_option(send_buffer_size_option, ec);
        if (ec) {
            return;
        }

        ip::tcp::socket::keep_alive keep_alive_option(true);
        socket.set_option(keep_alive_option, ec);
        if (ec) {
            return;
        }

        id = boost::str(boost::format("%1% (%2%)") % socket.local_endpoint().port() % addr);

        do_handshake(ec);
    }

private:
    void receive_and_check_archived_greeting(boost::system::error_code& ec)
    {
        // ArchiveD greeting
        // "ArchiveD v1.2.3 protocol 1.2\r\n"
        boost::asio::read_until(socket, recv_buffer, "\r\n", ec);
        if (ec) {
            return;
        }

        std::istream is(&recv_buffer);
        std::string line;
        if (!std::getline(is, line)) {
            LogVerbose(*this << " no handshake read from server");
            ec = error::make_error_code(error::bad_handshake);
            return;
        }

        boost::regex e("^ArchiveD v(\\d+\\.\\d+\\.\\d+) protocol (\\d+\\.\\d+)\\r");
        boost::smatch what;
        if (!boost::regex_match(line, what, e, boost::match_extra)) {
            LogVerbose(*this << " invalid handshake from server");
            ec = error::make_error_code(error::bad_handshake);
            return;
        }

        if (what[1].str() != version_number_only_string) {
            LogWarning(*this << " server version mismatch [ArchiveD: v" << what[1].str()
                             << " != v" << version_number_only_string << ']');
        }

        if (what[2].str() != archiving::put_protocol_version_str) {
            LogError(*this << " server protocol version (" << what[2].str() << ") not same as "
                           << archiving::put_protocol_version_str);
            ec = error::make_error_code(error::bad_protocol_version);
            return;
        }
    }

    void do_handshake(boost::system::error_code& ec)
    {
        bool handshake_error = false;
        receive_and_check_archived_greeting(ec);
        std::ostream os(&send_buffer);
        if (ec) {
            if (ec.category() != error::get_category()) {
                return;
            }

            handshake_error = true;
            os << "ERR " << ec.message() << "\r\n";
        } else {
            os << "OK CollectD v" << version_number_only_string << " protocol " << archiving::put_protocol_version_str
               << "\r\n";
        }

        boost::asio::write(socket, send_buffer, ec);
        if (handshake_error || ec) {
            return;
        }

        // read response from ArchiveD
        // OK or ERR with message
        boost::asio::read_until(socket, recv_buffer, "\r\n", ec);
        if (ec) {
            return;
        }

        std::istream is(&recv_buffer);
        std::string line;
        if (!std::getline(is, line)) {
            LogVerbose(*this << " no handshake-response read from server");
            ec = error::make_error_code(error::bad_handshake);
            return;
        }

        if (line != "OK\r") {
            LogError(*this << " handshake-error; ArchiveD says : " << line.substr(0, line.length() - 1));
            ec = error::make_error_code(error::bad_handshake);
        }
    }
};

std::ostream& operator<<(std::ostream& os, const connection& c)
{
    return os << c.id;
}

} // namespace archived
} // namespace net
} // namespace common

#endif //ARCHIVING_COMMON_NET_ARCHIVED_CONNECTION_HPP
