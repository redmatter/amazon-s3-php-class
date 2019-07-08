//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 20/06/2019.
//

#ifndef ARCHIVING_SEND_FILE_HPP
#define ARCHIVING_SEND_FILE_HPP

#include "connection.hpp"
#include "detail/put_file_session.hpp"

#include "../../archivable.hpp"

#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <boost/system/error_code.hpp>
#include <boost/chrono/system_clocks.hpp>
#include <boost/bind.hpp>
#include <boost/bind/protect.hpp>

namespace common
{
namespace net
{
namespace archived
{

void send_file_impl(const boost::system::error_code& ec, std::size_t bytes_transferred,
                    const boost::shared_ptr<connection>& conn,
                    const boost::shared_ptr<detail::put_file_session>& session) try
{
    if (ec) {
        conn->fault = ec;
        LogWarning(*conn << ' ' << *session << " network/protocl error; err:" << ec.message());
        session->post_error(conn->io_service, ec);
    }

    switch (session->state) {
    case detail::put_state::init: {
        session->state = detail::put_state::init_ack_waiting;
        boost::asio::async_read_until(conn->socket, conn->recv_buffer, "\r\n", conn->strand.wrap(
                boost::bind(send_file_impl, boost::asio::placeholders::error, 0, conn, session)));
        break;
    }

    case detail::put_state::init_ack_waiting: {
        std::string line;
        std::istream istr(&conn->recv_buffer);

        // if we can't get the response line from buffer
        if (!std::getline(istr, line)) {
            LogMessage(*conn << ' ' << *session << " no ack received for PUT");
            session->post_error(conn->io_service, error::put_initialisation_ack_not_received);
            return;
        }

        DevDebug(*conn << ' ' << *session << " PUT (init) ACK [" << line << "]");

        // if we have an ERR (line does not start with "OK PUT ")
        if (line.compare(0, 7, "OK PUT ") != 0) {
            // here we have an error response from ArchiveD
            // "ERR PUT <type> <org-id> <uuid> <error-message>\r\n"
            boost::iterator_range<std::string::iterator> r = boost::find_nth(line, " ", 4);
            std::string message(line, std::distance(line.begin(), r.end()), std::distance(r.end(), line.end()) - 1);
            LogWarning(*conn << ' ' << *session << " (PUT) negative ACK from ArchiveD; err:" << message);
            conn->fault = error::make_error_code(error::put_initialisation_ack_negative);
            session->post_error(conn->io_service, conn->fault);
            return;
        }

        LogMessage(*conn << ' ' << *session << " PUT OK; Sending file");
        if (!session->prepare()) {
            session->post_error(conn->io_service, error::put_send_file_failure_general);
            return;
        }

        session->state = detail::put_state::sending_file;
        conn->io_service.post(conn->strand.wrap(
                boost::bind(send_file_impl, boost::system::error_code(), 0, conn, session)));
        break;
    }

    case detail::put_state::sending_file: {
        const std::size_t buf_length = std::min<std::size_t>(session->file_size_remaining, 4096);
        std::vector<char> buf(buf_length + 1);

        if (!session->file_stream->read(&buf[0], buf_length)) {
            // failed ot read from file, midway.
            // this will leave this instance of the connection unusable; marking it faulty.
            conn->fault = error::make_error_code(error::put_send_file_failure_reading_file);
            session->post_error(conn->io_service, conn->fault);
            return;
        }

        const std::streamsize bytes_read = session->file_stream->gcount();
        session->file_size_remaining -= static_cast<std::size_t>(bytes_read);

        std::ostream ostr(&conn->send_buffer);
        ostr.write(&buf[0], bytes_read);

        // have we finished copying data from the file?
        if (session->file_size_remaining == 0) {
            session->state = detail::put_state::file_sent_ok;
            // add EOF
            ostr.put('\x1a');
        }

        boost::asio::async_write(conn->socket, conn->send_buffer, conn->strand.wrap(
                boost::bind(send_file_impl,
                            boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred,
                            conn, session)));

        break;
    }

    case detail::put_state::file_sent_ok: {
        session->state = detail::put_state::file_sent_ack_waiting;
        boost::asio::async_read_until(conn->socket, conn->recv_buffer, "\r\n", conn->strand.wrap(
                boost::bind(send_file_impl, boost::asio::placeholders::error, 0, conn, session)));

        // wait for one second per 100KB; ArchiveD needs time to compute MD5, and do other housekeeping
        // todo set_reconnect_timer(std::max(static_cast<const int>(put_file_size_ / 102400), 20), true);

        break;
    }

    case detail::put_state::file_sent_ack_waiting: {
        std::string line;
        std::istream istr(&conn->recv_buffer);

        // if we can't get the response line from buffer
        if (!std::getline(istr, line)) {
            LogMessage(*conn << ' ' << *session << " no ack received for PUT");
            conn->fault = error::make_error_code(error::put_send_file_failure_ack_not_received);
            session->post_error(conn->io_service, conn->fault);
            return;
        }

        DevDebug(*conn << ' ' << *session << " PUT (file-sent) ACK [" << line << "]");

        // if we have an ERR (line does not start with "OK RECV ")
        if (line.compare(0, 8, "OK RECV ") != 0) {
            // here we have an error response from ArchiveD
            // "ERR RECV <type> <org-id> <uuid> <error-message>\r\n"
            boost::iterator_range<std::string::iterator> r = boost::find_nth(line, " ", 4);
            std::string message(line, std::distance(line.begin(), r.end()), std::distance(r.end(), line.end()) - 1);
            LogWarning(*conn << ' ' << *session << " (RECV) negative ACK from ArchiveD; err:" << message);
            session->post_error(conn->io_service, error::put_send_file_failure_ack_negative);
            return;
        }

        session->state = detail::put_state::done;

        LogMessage(*conn << ' ' << *session << " PUT successful");
        session->post_success(conn->io_service);

        break;
    }

    // will never get here!
    case detail::put_state::done:
        break;
    }
} catch (const std::exception& e) {
    LogWarning(*conn << ' ' << *session << " PUT failed; exception:" << e.what());
    conn->fault = error::make_error_code(error::put_send_file_failure_general);
    session->post_error(conn->io_service, conn->fault);
}

void send_file(const boost::shared_ptr<connection>& conn,
               const boost::shared_ptr<archiving::archivable>& file,
               const boost::function<void(boost::system::error_code& ec)>& done_callback) try
{
    std::ostream ostr(&conn->send_buffer);
    if (!file->inject_put_request(ostr)) {
        conn->io_service.post(
                boost::bind(boost::protect(done_callback), error::make_error_code(error::put_initialisation_failed)));
        return;
    }

    // todo move update to last_send_file_time to the end of transfer
    conn->last_send_file_time = boost::chrono::steady_clock::now();

    boost::shared_ptr<detail::put_file_session> pfs =
            boost::make_shared<detail::put_file_session>(file, done_callback);
    boost::asio::async_write(conn->socket, conn->send_buffer, conn->strand.wrap(
            boost::bind(send_file_impl,
                        boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred,
                        conn, pfs)));
} catch (const std::exception& e) {
    LogWarning(*conn << ' ' << *file << " PUT initialisation failed; exception:" << e.what());
    conn->io_service.post(
            boost::bind(boost::protect(done_callback), error::make_error_code(error::put_initialisation_failed)));
}

} // namespace archived
} // namespace net
} // namespace common

#endif //ARCHIVING_SEND_FILE_HPP
