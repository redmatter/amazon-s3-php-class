//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 20/06/2019.
//

#ifndef ARCHIVING_SEND_NOP_HPP
#define ARCHIVING_SEND_NOP_HPP

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
    std::ostream ostr(&conn->send_buffer);
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

    conn->last_activity_time = boost::chrono::steady_clock::now();
}

} // namespace archived
} // namespace net
} // namespace common

#endif //ARCHIVING_SEND_NOP_HPP
