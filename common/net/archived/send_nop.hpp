//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 20/06/2019.
//

#ifndef ARCHIVING_SEND_NOP_HPP
#define ARCHIVING_SEND_NOP_HPP

#include "../../stream_debug.hpp"

#include "connection.hpp"

namespace common
{
namespace net
{
namespace archived
{

/**
 * send NOP to ArchiveD
 *
 * @param ec
 */
void send_nop(const boost::shared_ptr<connection>& conn, boost::system::error_code& ec)
{
    make_ostream(ostr, &conn->send_buffer);
    ostr.clear();
    ostr << "NOP\r\n\r\n";
    boost::asio::write(conn->socket, conn->send_buffer, ec);
    if (ec) {
        conn->fault = ec;
        return;
    }

    boost::asio::read_until(conn->socket, conn->recv_buffer, "\r\n", ec);
    if (ec) {
        conn->fault = ec;
        return;
    }

    std::string line;
	make_istream(istr, &conn->recv_buffer);

    // if we can't get the response line from buffer
    if (!std::getline(istr, line)) {
        LogMessage(*conn << " no ack received for NOP");
        conn->fault = error::make_error_code(error::nop_ack_not_received);
        ec = conn->fault;
        return;
    }

    DevDebug(*conn << " NOP ACK [" << line << "]");

    if (line.compare(0, 2, "OK") != 0) {
        conn->fault = error::make_error_code(error::put_send_file_failure_ack_not_received);
        ec = conn->fault;
    }
}

} // namespace archived
} // namespace net
} // namespace common

#endif //ARCHIVING_SEND_NOP_HPP
