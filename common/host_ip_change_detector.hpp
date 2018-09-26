//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 13/09/2018.
//

#ifndef ARCHIVING_HOST_IP_CHANGE_DETECTOR_HPP
#define ARCHIVING_HOST_IP_CHANGE_DETECTOR_HPP

#include <rml/logger.hpp>

#include <boost/atomic.hpp>
#include <boost/optional.hpp>
#include <string>
#include <netdb.h>
#include <time.h>

namespace archiving
{
struct ip_change_result
{
    typedef void (ip_change_result::* safe_bool_type)();

    bool result;
    uint32_t ip_address;

    ip_change_result() : result(false), ip_address(0)
    {}

    ip_change_result(bool result, uint32_t ip_address) : result(result), ip_address(ip_address)
    {}

    operator safe_bool_type() const
    { return result ? &ip_change_result::__bool : 0; }

    ip_change_result& operator=(const ip_change_result& other)
    {
        result = other.result;
        ip_address = other.ip_address;

        return *this;
    }

private:
    void __bool()
    {}
};

struct host_ip_change_detector
{
    const std::string hostname;
    const std::string service;
    boost::atomic_uint32_t ip_address;

    host_ip_change_detector(const std::string& hostname, const std::string& service) :
            hostname(hostname), service(service), ip_address(0)
    {}

    inline
    ip_change_result
    is_changed(const ip_change_result& last_result) const
    {
        const uint32_t ip_address_(ip_address.load());

        // till archived_clients connect and the change-detector has its first run, we fail open
        if (last_result.ip_address == 0 || ip_address_ == 0) {
            return ip_change_result(false, 0);
        }

        return ip_change_result(last_result.ip_address != ip_address_, ip_address_);
    }

    inline void
    detect_change()
    {
        addrinfo* result = 0;
        addrinfo hints;
        memset(&hints, 0, sizeof hints); // make sure the struct is empty
        hints.ai_family = AF_UNSPEC;     // don't care IPv4 or IPv6
        hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
        hints.ai_flags = AI_PASSIVE;     // fill in my IP for me

        if (0 != getaddrinfo(hostname.c_str(), service.c_str(), &hints, &result)) {
            LogWarning("Couldn't resolve host:" << hostname << " service/port:" << service);
        } else {
            for (addrinfo* res = result; res != NULL; res = res->ai_next) {
                uint32_t new_ip_address = ((struct sockaddr_in*) res->ai_addr)->sin_addr.s_addr;
                if (new_ip_address != ip_address) {
                    ip_address.store(new_ip_address, boost::memory_order_acquire);
                }
            }
            freeaddrinfo(result);
        }
    }
};
} // namespace archiving

#endif //ARCHIVING_HOST_IP_CHANGE_DETECTOR_HPP
