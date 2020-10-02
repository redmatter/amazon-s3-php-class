//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 18/06/2019.
//

#ifndef ARCHIVED_NET_PUT_ERROR_HPP
#define ARCHIVED_NET_PUT_ERROR_HPP

#include <boost/system/error_code.hpp>

namespace archiving {
namespace archived {
namespace net {
namespace put {
namespace error {

enum enum_t
{
    no_handshake,
    bad_handshake,
    bad_handshake_client_error,
    bad_protocol_version,

    no_command,
    bad_command,

    invalid_state_socket_closed,
};

bool
should_disconnect(enum_t err)
{
    return err == no_handshake || err == bad_handshake_client_error || err == invalid_state_socket_closed;
}

namespace detail {

class category : public boost::system::error_category
{
public:
    const char* name() const { return "archived.net.put"; }

    std::string message(int ev) const
    {
        switch (static_cast<enum_t>(ev)) {
        case no_handshake:
            return "no handshake";
        case bad_handshake:
            return "bad handshake";
        case bad_handshake_client_error:
            return "bad handshake; client error";
        case bad_protocol_version:
            return "protocol version mismatch";

        case no_command:
            return "no command received";
        case bad_command:
            return "invalid command received";

        case invalid_state_socket_closed:
            return "invalid state; socket closed";
        }

        return "common.net error";
    }
};

} // namespace detail

const boost::system::error_category&
get_category()
{
    static detail::category instance;
    return instance;
}

} // namespace error
} // namespace put
} // namespace net
} // namespace archived
} // namespace archiving

namespace boost {
namespace system {

template<>
struct is_error_code_enum<archiving::archived::net::put::error::enum_t>
{
    static const bool value = true;
};

} // namespace system
} // namespace boost

namespace archiving {
namespace archived {
namespace net {
namespace put {
namespace error {

inline boost::system::error_code
make_error_code(const enum_t e)
{
    return boost::system::error_code(static_cast<int>(e), get_category());
}

bool
should_disconnect(boost::system::error_code ec)
{
    return ec.category() == get_category() && should_disconnect(static_cast<enum_t>(ec.value()));
}

} // namespace error
} // namespace put
} // namespace net
} // namespace archived
} // namespace archiving

#endif // ARCHIVED_NET_PUT_ERROR_HPP
