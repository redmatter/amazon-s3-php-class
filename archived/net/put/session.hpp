//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 29/09/2020.
//

#ifndef ARCHIVED_NET_PUT_SESSION_HPP
#define ARCHIVED_NET_PUT_SESSION_HPP

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/bind.hpp>
#include <boost/bind/protect.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/optional.hpp>
#include <boost/regex.hpp>

#include <rml/logger.hpp>
#include <rml/version.h>

#include <defines.h>
#include <stream_debug.hpp>
#include <types.hpp>
#include <utils/disable_skipws.hpp>
#include <utils/verify_char.hpp>

#include "../../types.hpp"
#include "../response.hpp"
#include "error.hpp"
#include "exception.hpp"
#include "request.hpp"

namespace {
enum
{
    MaxBufferLength = 128 * 1024
};
}

namespace archiving {
namespace archived {
namespace net {
namespace put {

namespace state {
enum state_t
{
    accepting = 0,

    handshake_sent,
    handshake_recv,

    next_command,

    put_received_ack_ok,
    put_receiving_file,
    put_receiving_file_ok,

    closing,
    closed,

    n_states
};

static const char*
to_string(const state_t& state)
{
    static const char* str[n_states + 1] = {
        "accepting",

        "handshake_sent",
        "handshake_recv",

        "next_command",

        "put_received_ack_ok",
        "put_receiving_file",
        "put_receiving_file_ok",

        "closing",
        "closed",

        "invalid" // n_states
    };

    if (state >= accepting && state < n_states) {
        return str[state];
    }

    return str[n_states];
}

} // namespace state

struct session;

std::ostream&
operator<<(std::ostream&, const session&);

class session : public boost::enable_shared_from_this<session>
{
    typedef common::utils::verify_char<char> verify_char;
    typedef common::utils::disable_skipws disable_skipws;
    typedef app::types::metadata metadata_type;

    friend std::ostream& operator<<(std::ostream& out, const session& o);

    boost::asio::io_service& io_service;
    boost::asio::strand strand;
    boost::asio::ip::tcp::socket socket;

    const std::string cache_dir;
    const boost::function<boost::optional<std::time_t>()> should_neglect_pcaps;
    const boost::function<bool(archiving::org_id_t, app::policy::id_type)> validate_policy;
    /**
     * request_handler should throw an exception to indicate that it could not
     *     handle the request. request_handler should avoid any heavy / long-running
     *     processing as it will hold-up the io_service thread and the connection.
     */
    const boost::function<void(boost::shared_ptr<request>)> request_handler;

    boost::asio::streambuf recv_buffer;
    boost::asio::streambuf send_buffer;

    boost::array<char, MaxBufferLength> buf;

protected:
    // for logging / debug, composed of local-port and IP address
    std::string id;
    state::state_t current_state;

public:
    session(boost::asio::io_service& io_service,
            const std::string& cache_dir,
            const boost::function<boost::optional<std::time_t>()>& should_neglect_pcaps,
            const boost::function<bool(archiving::org_id_t, app::policy::id_type)>& validate_policy,
            const boost::function<void(boost::shared_ptr<request>)>& request_handler)
        : io_service(io_service)
        , strand(io_service)
        , socket(io_service)
        , cache_dir(cache_dir)
        , should_neglect_pcaps(should_neglect_pcaps)
        , validate_policy(validate_policy)
        , request_handler(request_handler)
        , buf()

        , id("<not-connected>")
        , current_state(state::accepting)
    {}

    ~session() { LogVerbose(*this << "destroyed"); }

    boost::asio::ip::tcp::socket& get_socket() { return socket; }

    void start(bool no_delay, std::size_t receive_buffer_size)
    {
        boost::system::error_code ec;

        if (no_delay) {
            socket.set_option(boost::asio::ip::tcp::no_delay(true), ec);
            if (ec) {
                LogWarning(*this << "set_option:no_delay failed; err:" << ec.message());
                io_service.post(strand.wrap(boost::bind(&session::do_close, shared_from_this())));
                return;
            }
        }

        if (receive_buffer_size > 0) {
            boost::asio::socket_base::receive_buffer_size option(/*16384*/receive_buffer_size);
            socket.set_option(option, ec);
            if (ec) {
                LogWarning(*this << "set_option failed; err:" << ec.message());
                io_service.post(strand.wrap(boost::bind(&session::do_close, shared_from_this())));
                return;
            }
        }

        set_id();

        current_state = state::handshake_sent;
        make_ostream(ostr, &send_buffer);
        ostr << "ArchiveD v" << version_number_only_string << " protocol " << archiving::put_protocol_version_str
             << "\r\n";

        async_send_buffer();
    }

private:
    session();
    session(const session&);

    void do_close()
    {
        current_state = state::closed;

        boost::system::error_code ec;
        socket.close(ec); // ignore any errors; we don't care at this point
    }

    void set_id()
    {
        boost::system::error_code ec;

        std::ostringstream ostr;

        boost::asio::ip::tcp::resolver resolver(io_service);
        boost::asio::ip::tcp::resolver::iterator itr = resolver.resolve(socket.remote_endpoint(), ec);
        if (!ec) {
            std::string fqdn((*itr).host_name());
            std::istringstream istr(fqdn);
            std::string hostname;
            std::getline(istr, hostname, '.');

            ostr << hostname << ':' << socket.remote_endpoint().port();
        } else {
            ostr << socket.remote_endpoint();
        }

        id = ostr.str();
    }

    template<typename Handler>
    inline void async_do(const Handler& f)
    {
        io_service.post(strand.wrap(f));
    }

    inline void async_continue_statemachine()
    {
        static const boost::system::error_code no_error;
        async_do(boost::bind(&session::run_statemachine, shared_from_this(), boost::cref(no_error)));
    }

    inline void async_continue_statemachine(state::state_t s)
    {
        current_state = s;
        async_continue_statemachine();
    }

    inline void async_close(const error::enum_t err) { async_close(error::make_error_code(err)); }

    inline void async_close(const boost::system::error_code& err)
    {
        if (err) {
            async_close(err.message());
        } else {
            async_close();
        }
    }

    inline void async_close(const std::string& err)
    {
        LogError(*this << "closing due to error:" << err);
        async_close();
    }

    inline void async_close()
    {
        current_state = state::closing;
        async_do(boost::bind(&session::do_close, shared_from_this()));
    }

    static const boost::regex& greeting_ok_regex()
    {
        static const boost::regex re("^OK Collect[a-zA-Z]*D v(\\d+\\.\\d+\\.\\d+) protocol (\\d+\\.\\d+)\\r");
        return re;
    }

    static const boost::regex& err_response_regex()
    {
        static const boost::regex re("^ERR ([^\\r]+)\\r");
        return re;
    }

    void check_collectd_greeting(boost::system::error_code& ec)
    {
        make_istream(istr, &recv_buffer);

        std::string line;
        if (!std::getline(istr, line, '\n')) {
            ec = error::make_error_code(error::no_handshake);
            return;
        }

        boost::smatch what;

        if (boost::regex_match(line, what, err_response_regex(), boost::match_extra)) {
            LogWarning(*this << "error response for handshake; error:" << what[1].str());
            ec = error::make_error_code(error::bad_handshake_client_error);
            return;
        }

        if (!boost::regex_match(line, what, greeting_ok_regex(), boost::match_extra)) {
            LogVerbose(*this << "invalid handshake from client [" << line << "]");
            ec = error::make_error_code(error::bad_handshake);
            return;
        }

        if (what[1].str() != version_number_only_string) {
            LogWarning(*this << "client version mismatch [Collect*D: v" << what[1].str() << " != v"
                             << version_number_only_string << ']');
        }

        if (what[2].str() != archiving::put_protocol_version_str) {
            LogError(*this << "client protocol version (" << what[2].str() << ") not same as "
                           << archiving::put_protocol_version_str);
            ec = error::make_error_code(error::bad_protocol_version);
            return;
        }
    }

    template<typename Handler>
    inline void async_send_buffer(const Handler& handler)
    {
        boost::asio::async_write(socket, send_buffer, strand.wrap(handler));
    }

    inline void async_send_buffer()
    {
        async_send_buffer(
            boost::bind(&session::run_statemachine, shared_from_this(), boost::asio::placeholders::error));
    }

    template<typename Handler>
    inline void async_read_line(const Handler& handler)
    {
        boost::asio::async_read_until(socket, recv_buffer, "\r\n", strand.wrap(handler));
    }

    inline void async_read_line()
    {
        async_read_line(boost::bind(&session::run_statemachine, shared_from_this(), boost::asio::placeholders::error));
    }

    template<typename T>
    inline void async_send_response(const T& t)
    {
        make_ostream(ostr, &send_buffer);
        ostr << t << "\r\n";
        async_send_buffer();
    }

    template<typename T, typename Handler>
    inline void async_send_response(const T& t, const Handler& handler)
    {
        make_ostream(ostr, &send_buffer);
        ostr << t << "\r\n";
        async_send_buffer(handler);
    }

    inline void async_send_response(const boost::system::error_code& ec)
    {
        if (!ec) {
            async_send_response_ok();
            return;
        }

        make_ostream(ostr, &send_buffer);
        ostr << "ERR " << ec.message() << "\r\n";
        async_send_buffer();
    }

    inline void async_send_response_ok() { async_send_response("OK"); }

    void handle_command(const boost::system::error_code& ec)
    try {
        const verify_char space(' ');
        const verify_char cr('\r');
        const verify_char lf('\n');

        if (ec) {
            async_close(ec);
            return;
        }

        make_istream(istr, &recv_buffer);

        disable_skipws skipws_off(istr);

        std::string verb;

        if (!(istr >> verb)) {
            async_close(error::no_command);
            return;
        }

        if (verb == "NOP" || verb == "BYE") {
            istr >> cr >> lf >> cr >> lf;

            const bool is_nop = verb == "NOP";

            current_state = is_nop ? state::next_command : state::closing;
            async_send_response_ok();

            // for BYE, close immediately (after responding OK)
            if (!is_nop) {
                async_close();
            }

            return;
        }

        // at this point we expect PUT only
        if (verb != "PUT") {
            async_close(error::bad_command);
            return;
        }

        archiving::file_type::file_type_t file_type;
        archiving::org_id_t org_id;
        std::string uuid;
        int64_t content_length = 0;

        istr >> space >> file_type >> space >> org_id >> space >> uuid >> space >> content_length >> cr >> lf;
        if (!istr || content_length < 0) {
            async_close(error::bad_command);
            return;
        }

        // roughly looks like a PUT request, let us handle it
        handle_put(file_type, org_id, uuid, content_length);
    } catch (const exception::bad_protocol& e) {
        LogError(*this << "bad_protocol: " << e.what());
        current_state = state::closing;
        async_send_response(put::bad_request(e.what()));
    } catch (const std::exception& e) {
        LogError(*this << "exception: " << e.what());
        current_state = state::closing;
        async_send_response(put::internal_error(e.what()));
    }

    inline void discard_current_put_request()
    {
        recv_buffer.consume(boost::asio::read_until(socket, recv_buffer, "\r\n\r\n"));
    }

    inline void discard_current_put_data()
    {
        recv_buffer.consume(boost::asio::read_until(socket, recv_buffer, "\x1a"));
    }

    void validate_policy_id(archiving::org_id_t org_id, app::policy::id_type policy_id) const
    try {
        if (policy_id == app::policy::pseudo_id::do_not_archive) {
            return;
        }

        // make sure we can handle this PolicyID before CollectD start sending data
        if (!validate_policy(org_id, policy_id)) {
            throw exception::bad_request("policy not found");
        }
    } catch (const exception::bad_request& e) {
        throw;
    } catch (const std::exception& e) {
        LogError(*this << "validate_policy_id : exception:" << e.what());
        throw exception::internal_error("error while validating policy");
    }

    boost::optional<std::time_t> get_neglect_pcaps_until()
    {
        try {
            return should_neglect_pcaps();
        } catch (const std::exception& e) {
            LogError(*this << "pcap neglect callback failed; exception:" << e.what());
            return std::time(0) + 60;
        }
    }

    void handle_put(archiving::file_type::file_type_t file_type,
                    archiving::org_id_t org_id,
                    const std::string& uuid,
                    std::size_t content_length)
    try {
        LogMessage(*this << file_type << '/' << org_id << '/' << uuid
                         << " PUT received - content-length:" << content_length);

        if (content_length == 0) {
            LogError(*this << "Invalid content length:" << content_length);
            discard_current_put_request();
            throw exception::bad_request("invalid content length");
        }

        if (org_id <= 0) {
            LogError(*this << "Invalid Org ID:" << org_id);
            discard_current_put_request();
            throw exception::bad_request("invalid Org ID");
        }

        if (file_type == archiving::file_type::pcap) {
            boost::optional<std::time_t> busy_until = get_neglect_pcaps_until();
            if (busy_until != boost::none) {
                discard_current_put_request();
                throw exception::server_busy(*busy_until, "neglecting PCAP");
            }
        }

        make_istream(istr, &recv_buffer);

        disable_skipws skipws_off(istr);

        boost::shared_ptr<request> req = boost::make_shared<request>(
            file_type, org_id, uuid, content_length, boost::make_shared<metadata_type>(boost::ref(istr)), cache_dir);

        validate_policy_id(org_id, req->policy_id);

        // reserve file space on disk
        req->make_file();

        current_state = state::put_received_ack_ok;
        async_send_response(
            put::continue_(file_type, org_id, uuid),
            boost::bind(&session::async_receive_put_data, shared_from_this(), req, boost::asio::placeholders::error));
    } catch (const exception::internal_error& e) {
        LogMessage(*this << file_type << '/' << org_id << '/' << uuid << " : internal_error:" << e.what());
        current_state = state::next_command;
        async_send_response(put::internal_error(file_type, org_id, uuid, e.what()));
    } catch (const exception::bad_request& e) {
        LogMessage(*this << file_type << '/' << org_id << '/' << uuid << " : bad_request:" << e.what());
        current_state = state::next_command;
        async_send_response(put::bad_request(file_type, org_id, uuid, e.what()));
    } catch (const exception::server_busy& e) {
        LogMessage(*this << file_type << '/' << org_id << '/' << uuid << " : server_busy:" << e.what());
        current_state = state::next_command;
        async_send_response(put::server_busy(file_type, org_id, uuid, e.until));
    }

    void async_receive_put_data(const boost::shared_ptr<request>& req, boost::system::error_code ec)
    {
        if (ec) {
            async_close(ec);
            return;
        }

        current_state = state::put_receiving_file;
        async_receive_put_data_impl(req, req->content_length);
    }

    void async_receive_put_data_impl(const boost::shared_ptr<request>& req, std::size_t remaining)
    {
        recv_buffer.prepare(16 * 1024);
        boost::asio::async_read(socket,
                                recv_buffer,
                                boost::asio::transfer_at_least(1),
                                strand.wrap(boost::bind(&session::async_receive_put_data_chunks,
                                                        shared_from_this(),
                                                        req,
                                                        remaining,
                                                        boost::asio::placeholders::error,
                                                        boost::asio::placeholders::bytes_transferred)));
    }

    void async_receive_put_data_chunks(const boost::shared_ptr<request>& req,
                                       std::size_t remaining,
                                       const boost::system::error_code& error,
                                       std::size_t bytes_transferred)
    try {
        if (error) {
            LogWarning(*this << req->storage_key << " error while during PUT (data): " << error.message());
            async_close();
            return;
        }

        LogDebug(*this << req->storage_key << " bytes_transferred: " << bytes_transferred);
        LogDebug(*this << req->storage_key << " content_length: " << req->content_length);

        while (bytes_transferred) {
            // if we have transferred the object fully, the next character should be EOF
            if (remaining == 0) {
                char eof;
                if (1 == recv_buffer.sgetn(&eof, 1)) {
                    bytes_transferred--;
                    if (eof == '\x1a') {
                        current_state = state::put_receiving_file_ok;
                        LogMessage(*this << req->storage_key << " PUT: data transfer completed");
                        break;
                    }
                }

                LogWarning(*this << req->storage_key << " PUT: EOF not found; unexpected data");
                async_continue_statemachine(state::closing);
                return;
            }

            std::size_t copy_size = std::min<std::size_t>(std::min(bytes_transferred, remaining), MaxBufferLength);
            copy_size = recv_buffer.sgetn(buf.c_array(), copy_size);
            req->get_file()->rdbuf()->sputn(buf.c_array(), copy_size);
            remaining -= copy_size;
            bytes_transferred -= copy_size;

            LogDebug(*this << req->storage_key << " xfr: " << remaining << '/' << req->content_length << " copied "
                           << copy_size << " bytes");
        }

        // if we haven't finished receiving data and have fully consumed the current chunk, continue receiving more...
        if (bytes_transferred == 0 && current_state == state::put_receiving_file) {
            async_receive_put_data_impl(req, remaining);
            return;
        }

        // if we have seen up to EOF, nothing more is expected to turn up from Collect*D until ACK / nACK
        if (bytes_transferred > 0) {
            LogWarning(*this << req->storage_key << " invalid state; bytes found beyond EOF");
            async_continue_statemachine(state::closing);
            return;
        }

        req->finalise();
        LogMessage(*this << req->storage_key << " PUT data received ok");

        request_handler(req);

        current_state = state::next_command;
        async_send_response(recv::accepted(req->file_type, req->org_id, req->uuid));
    } catch (const exception::bad_request& e) {
        LogMessage(*this << req->storage_key << " : bad_request:" << e.what());
        current_state = state::next_command;
        async_send_response(recv::bad_request(req->file_type, req->org_id, req->uuid, e.what()));
    } catch (const std::exception& e) {
        LogWarning(*this << req->storage_key << " exception; " << e.what());
        current_state = state::closing;
        async_send_response(recv::internal_error(req->file_type, req->org_id, req->uuid, e.what()));
    }

    void run_statemachine(boost::system::error_code ec)
    {
        if (ec) {
            async_close(ec);
            return;
        }

        switch (current_state) {
        case state::handshake_sent:
            current_state = state::handshake_recv;
            async_read_line();
            return;

        case state::handshake_recv:
            check_collectd_greeting(ec);
            if (error::should_disconnect(ec)) {
                async_close(ec);
                return;
            }

            current_state = ec ? state::closing : state::next_command;
            async_send_response(ec);
            return;

        case state::next_command:
            async_read_line(
                boost::bind(&session::handle_command, shared_from_this(), boost::asio::placeholders::error));
            return;

        // handled elsewhere ... if we see them here, it is an error
        case state::accepting:
        case state::put_received_ack_ok:
        case state::put_receiving_file:
        case state::put_receiving_file_ok:

        // these states are destined for explicit connection termination
        case state::closing:
        case state::closed: // anomaly, but does not harm to close again
        case state::n_states:
            async_close();
            break;
        }
    }
};

std::ostream&
operator<<(std::ostream& os, const session& c)
{
    return os << c.id << " [state:" << state::to_string(c.current_state) << "] ";
}

} // namespace put
} // namespace net
} // namespace archived
} // namespace archiving
#endif // ARCHIVED_NET_PUT_SESSION_HPP
