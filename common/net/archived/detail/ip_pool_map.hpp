//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 21/06/2019.
//

#ifndef ARCHIVING_IP_POOL_MAP_HPP
#define ARCHIVING_IP_POOL_MAP_HPP

#include <boost/array.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/atomic.hpp>
#include <boost/system/error_code.hpp>

#include <vector>
#include <exception>

#include <stdint.h>

namespace common
{
namespace net
{
namespace archived
{
namespace detail
{

/**
 * Implements a "handy" map of IP to its connection-pool.
 * This is intentionally kept silly-simple! Exclusive locking is done only when the map is updated.
 * Not many updates or values are expected in this map. At most 2 on a normal day; but can be 4 if we need to account
 * for a network change.
 *
 * @tparam Pool
 * @tparam N
 */
template<typename Pool, unsigned N>
class ip_pool_map
{
    boost::array<uint32_t, N> ips;
    boost::array<Pool, N> pools;

    std::size_t insertion_point;
    boost::mutex insertion_mutex;

    const boost::function<Pool(uint32_t, boost::system::error_code&)> create_callback;

    std::size_t
    get_index(const uint32_t ip) const
    {
        std::size_t i = insertion_point;
        while (i > 0) {
            if (ips[i - 1] == ip) {
                return i - 1;
            }

            i--;
        }

        return N;
    }

public:
    explicit ip_pool_map(const boost::function<Pool(uint32_t, boost::system::error_code&)>& create_callback) :
            insertion_point(0), create_callback(create_callback)
    {}

    Pool
    get_pool(uint32_t ip, boost::system::error_code& ec)
    {
        const std::size_t i = get_index(ip);
        if (i != N) {
            return pools[i];
        }

        Pool pool = create_callback(ip, ec);
        if (ec) {
            return Pool();
        }

        boost::unique_lock<boost::mutex> lock(insertion_mutex);

        if (insertion_point >= N) {
            // fudge; any error would do!
            ec = boost::system::posix::make_error_code(boost::system::posix::no_space_on_device);
            return Pool();
        }

        std::size_t idx = insertion_point++;

        ips[idx] = ip;
        pools[idx] = pool;

        return pool;
    }

    std::size_t
    size() const
    {
        return insertion_point;
    }

    void
    for_each(boost::function<void(uint32_t, Pool&)> f)
    {
        std::size_t i = insertion_point;
        while (i > 0) {
            f(ips[i - 1], pools[i - 1]);
            i--;
        }
    }
};


} // namespace detail
} // namespace archived
} // namespace net
} // namespace common

#endif //ARCHIVING_IP_POOL_MAP_HPP
