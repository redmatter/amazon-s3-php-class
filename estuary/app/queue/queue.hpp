//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 14/12/2017.
//

#ifndef ARCHIVING_ESTUARY_APP_QUEUE_BASE_QUEUE_HPP
#define ARCHIVING_ESTUARY_APP_QUEUE_BASE_QUEUE_HPP

#include <boost/enable_shared_from_this.hpp>
#include <boost/container/list.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/atomic.hpp>
#include <boost/bind.hpp>
#include <boost/chrono/system_clocks.hpp>
#include "error.hpp"
#include "../../../common/collectd/callbacks.hpp"

namespace app
{
namespace queue
{
template<class T>
class base_queue : public boost::enable_shared_from_this<base_queue<T> >
{
    typedef boost::container::list<T> list_type;

    list_type list;

    boost::mutex mutex;
    boost::condition_variable not_empty_condition;
    boost::condition_variable not_full_condition;

    unsigned max_capacity;
    boost::atomic_bool finishing;

    inline bool is_finishing_or_any_consumable()
    {
        return is_finishing() || any_consumable();
    }

    inline bool is_finishing_or_has_room()
    {
        return is_finishing() || has_room();
    }

    inline bool is_finishing() const
    {
        return finishing;
    }

    inline bool any_consumable() const
    {
        return !list.empty();
    }

    inline bool has_room() const
    {
        return max_capacity == 0 // if the max-capacity is 0, ignore it
               || list.size() < max_capacity;
    }

public:
    explicit base_queue(const unsigned max_capacity = 0) : max_capacity(max_capacity), finishing(false)
    {}

    inline bool set_finishing()
    {
        bool expected = false;
        const bool changed(finishing.compare_exchange_strong(expected, true));
        if (changed) {
            // wake everyone up; time to give up!
            not_full_condition.notify_all();
            not_empty_condition.notify_all();
        }

        return changed;
    }

    inline unsigned long size()
    {
        boost::mutex::scoped_lock lock(mutex);
        return list.size();
    }

    template<class Rep, class Period>
    void
    produce(const T& item, const boost::chrono::duration<Rep, Period> wait_duration, boost::system::error_condition& ec)
    {
        produce(item, boost::chrono::steady_clock::now() + wait_duration, ec);
    };

    void produce(const T& item, boost::chrono::steady_clock::time_point wait_until, boost::system::error_condition& ec)
    {
        boost::mutex::scoped_lock lock(mutex);

        if (not_full_condition.wait_until(
                lock, wait_until, boost::bind(&base_queue::is_finishing_or_has_room, this->shared_from_this()))) {

            if (is_finishing()) {
                ec = error::make_error_condition(error::runtime_errors::is_finishing);
            } else {
                list.push_front(item);

                lock.unlock();
                not_empty_condition.notify_one();
            }
        } else {
            ec = error::make_error_condition(error::runtime_errors::is_full);
        }
    }

    void produce(const T& item, boost::system::error_condition& ec)
    {
        while (true) {
            boost::system::error_condition ec_local;

            produce(item, boost::chrono::seconds(1), ec_local);

            if (!ec_local) {
                break;
            }

            if (ec_local != error::make_error_condition(error::runtime_errors::is_full)) {
                ec = ec_local;
                break;
            }
        }
    }

    template<class Rep, class Period>
    void consume(T& item, const boost::chrono::duration<Rep, Period>& wait_duration, boost::system::error_condition& ec)
    {
        consume(item, boost::chrono::steady_clock::now() + wait_duration, ec);
    }

    void consume(T& item, const boost::chrono::steady_clock::time_point& wait_until, boost::system::error_condition& ec)
    {
        boost::mutex::scoped_lock lock(mutex);

        if (not_empty_condition.wait_until(
                lock, wait_until, boost::bind(&base_queue::is_finishing_or_any_consumable, this->shared_from_this()))) {

            if (is_finishing()) {
                ec = error::make_error_condition(error::runtime_errors::is_finishing);
            } else {
                item = list.back();
                list.pop_back();

                lock.unlock();
                not_full_condition.notify_one();
            }
        } else {
            ec = error::make_error_condition(error::runtime_errors::is_empty);
        }
    }

    void consume(T& item, boost::system::error_condition& ec)
    {
        while (true) {
            boost::system::error_condition ec_local;

            consume(item, boost::chrono::seconds(1), ec_local);

            if (!ec_local) {
                break;
            }

            if (ec_local != error::make_error_condition(error::runtime_errors::is_empty)) {
                ec = ec_local;
                break;
            }
        }
    }
};
} // namespace queue
} // namespace app
#endif //ARCHIVING_ESTUARY_APP_QUEUE_BASE_QUEUE_HPP
