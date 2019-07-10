//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 20/06/2019.
//

#ifndef ARCHIVING_IP_ADDR_UTILS_HPP
#define ARCHIVING_IP_ADDR_UTILS_HPP

#include <stdint.h>
#include <boost/asio/ip/address.hpp>

namespace common
{
namespace net
{
namespace utils
{

/**
 * Convert asio ip::address to database friendly uint32
 * Rationale: No direct access to asio ip::address internals to do this.
 *
 * @param addr
 * @param ec
 * @return
 */
uint32_t
ip_to_uint32(const boost::asio::ip::address& addr, boost::system::error_code& ec)
{
    in_addr in; // NOLINT(cppcoreguidelines-pro-type-member-init)
    if (1 != inet_pton(AF_INET, addr.to_string().c_str(), &in)) {
        ec = boost::system::error_code(errno, boost::system::system_category());
        return 0;
    }

    return in.s_addr;

}

/**
 * Convert database friendly uint32 to asio ip::address
 * Rationale: No direct access to asio ip::address internals to do this.
 *
 * @param addr
 * @param ec
 * @return
 */
boost::asio::ip::address
uint32_to_ip(uint32_t addr, boost::system::error_code& ec)
{
    // zero value will always translate to zero-value of ip::address
    if (addr == 0) {
        return boost::asio::ip::address();
    }

    char str[INET_ADDRSTRLEN];
    in_addr in; // NOLINT(cppcoreguidelines-pro-type-member-init)
    in.s_addr = addr;
    if (0 == inet_ntop(AF_INET, &in, str, INET_ADDRSTRLEN)) {
        ec = boost::system::error_code(errno, boost::system::system_category());
        return boost::asio::ip::address();
    }

    return boost::asio::ip::address::from_string(str, ec);
}

} // namespace utils
} // namespace net
} // namespace common
#endif //ARCHIVING_IP_ADDR_UTILS_HPP
