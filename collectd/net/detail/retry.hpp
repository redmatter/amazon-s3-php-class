//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 20/02/2020.
//

#ifndef ARCHIVING_COLLECTD_NET_DEATIL_RETRY_HPP
#define ARCHIVING_COLLECTD_NET_DEATIL_RETRY_HPP

#include "../../../common/archivable.hpp"

#include <boost/chrono/system_clocks.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>

#include <set>

namespace collectd {
namespace net {
namespace detail {

template<typename T>
struct retry_item
{
    const boost::chrono::steady_clock::time_point when;
    const T archivable;

    bool operator<(retry_item const& r) const { return when < r.when; }

    retry_item(const boost::chrono::steady_clock::time_point& when, const T& archivable)
        : when(when), archivable(archivable)
    {}
};

template<typename T>
struct base_retry_manager
{
    typedef retry_item<T> item_type;
    typedef std::multiset<item_type> item_set;

    item_set items;
    mutable boost::mutex mutex;

    inline void add(const boost::chrono::steady_clock::time_point& when, const T& archivable)
    {
        boost::unique_lock<boost::mutex> lock(mutex);
        items.insert(item_type(when, archivable));
    }

    inline std::size_t count() const
    {
        boost::unique_lock<boost::mutex> lock(mutex);
        return items.size();
    }

    inline std::size_t schedule_batch(const boost::chrono::steady_clock::time_point& upto_when,
                                      std::size_t size,
                                      const boost::function<void(T)>& callback)
    {
        boost::unique_lock<boost::mutex> lock(mutex);
        if (items.empty()) {
            return 0;
        }

        std::size_t count = 0;
        while (count < size) {
            typename item_set::iterator begin = items.begin();
            if (begin == items.end()) {
                break;
            }

            if (items.begin()->when > upto_when) {
                break;
            }

            callback(items.begin()->archivable);
            items.erase(items.begin());
            count++;
        }

        return count;
    }

    inline std::size_t schedule_batch(std::size_t size, const boost::function<void(T)>& callback)
    {
        return schedule_batch(boost::chrono::steady_clock::now(), size, callback);
    }
};

typedef base_retry_manager<boost::shared_ptr<archiving::archivable> > retry_manager;

} // namespace detail
} // namespace net
} // namespace collectd
#endif // ARCHIVING_COLLECTD_NET_DEATIL_RETRY_HPP
