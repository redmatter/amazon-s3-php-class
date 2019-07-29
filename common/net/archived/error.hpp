//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 18/06/2019.
//

#ifndef ARCHIVING_COMMON_NET_ERROR_HPP
#define ARCHIVING_COMMON_NET_ERROR_HPP

#include <boost/system/error_code.hpp>

namespace common
{
namespace net
{
namespace archived
{
namespace error
{

enum enum_t
{
    cannot_resolve,

    bad_handshake,
    bad_protocol_version,

    invalid_state_socket_closed,

    put_initialisation_failed,
    put_initialisation_ack_not_received,
    put_initialisation_ack_negative,
    put_send_file_failure_reading_file,
    put_send_file_failure_ack_not_received,
    put_send_file_failure_ack_negative,
    put_send_file_failure_general,
    put_interrupted,

    nop_ack_not_received,
    nop_ack_negative
};

namespace detail
{

class category : public boost::system::error_category
{
public:
    const char* name() const
    {
        return "common.net";
    }

    std::string message(int ev) const
    {
        switch (static_cast<enum_t>(ev)) {
        case cannot_resolve:
            return "cannot resolve";
        case bad_handshake:
            return "bad handshake";
        case bad_protocol_version:
            return "protocol version mismatch";
        case invalid_state_socket_closed:
            return "invalid state; socket closed";
        case put_initialisation_failed:
            return "put initialisation failed";
        case put_initialisation_ack_not_received:
            return "put initialisation failed (no ACK received)";
        case put_initialisation_ack_negative:
            return "put initialisation failed (Negative ACK)";
        case put_send_file_failure_reading_file:
            return "put send-file failed (reading from file)";
        case put_send_file_failure_general:
            return "put send-file failed (general)";
        case put_send_file_failure_ack_not_received:
            return "put send-file failed (no ACK received)";
        case put_send_file_failure_ack_negative:
            return "put send-file failed (Negative ACK)";
        case put_interrupted:
            return "put interrupted";
        case nop_ack_not_received:
            return "nop failed (no ACK received)";
        case nop_ack_negative:
            return "nop failed (Negative ACK)";
        }

        return "common.net error";
    }
};

} // namespace detail

const boost::system::error_category& get_category()
{
    static detail::category instance;
    return instance;
}

} // namespace error
} // namespace archived
} // namespace net
} // namespace common

namespace boost
{
namespace system
{

template<>
struct is_error_code_enum<common::net::archived::error::enum_t>
{
    static const bool value = true;
};

} // namespace system
} // namespace boost

namespace common
{
namespace net
{
namespace archived
{
namespace error
{

inline boost::system::error_code make_error_code(const enum_t e)
{
    return boost::system::error_code(static_cast<int>(e), get_category());
}

} // namespace error
} // namespace archived
} // namespace net
} // namespace common

#endif //ARCHIVING_COMMON_NET_ERROR_HPP
