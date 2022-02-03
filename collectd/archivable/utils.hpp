//
// Created by dkorah on 04/07/17.
//

#ifndef COLLECTD_ARCHIVABLE_UTILS_HPP
#define COLLECTD_ARCHIVABLE_UTILS_HPP

#include "../../common/archivable.hpp"
#include "../../common/collectd/callbacks.hpp"

#include <boost/functional/hash.hpp>

namespace archiving {

std::size_t hash_value(archivable const &a)
{
    return boost::hash<std::string>()(a.get_uuid());
}

bool operator==(const archivable &x, const archivable &y)
{
    // IMPORTANT NOTE:
    // Do not compare OrgID as it can potentially be uninitialised.
    // Worth noting that OrgID is figured out lazily!

    return x.get_type() == y.get_type() &&
           x.get_uuid() == y.get_uuid();
}

bool operator<(const archivable &x, const archivable &y)
{
    const archiving::file_type::file_type_t& x_file_type(x.get_type());
    const archiving::file_type::file_type_t& y_file_type(y.get_type());

    // IMPORTANT NOTE:
    // Do not compare OrgID as it can potentially be uninitialised.
    // Worth noting that OrgID is figured out lazily!

    if (x_file_type != y_file_type) {
        return x_file_type < y_file_type;
    }

    return x.get_uuid() < y.get_uuid();
}

/**
 * @brief Deduce delay duration based on call-duration. Here the assumption is that a recording will take longer to be
 *        ready after the CDR has been written to disk. The expectation is that once we wait long enough we will be able
 *        to "safely" read the file without any race condition.
 *
 * @tparam Rep for chrono::duration
 * @tparam Period for chrono::duration
 * @param d
 * @return
 */
template <typename Rep, typename Period>
inline boost::chrono::seconds
call_duration_to_delay(const boost::chrono::duration<Rep, Period>& d)
{
    // convert to seconds and round-up
    boost::chrono::seconds seconds = boost::chrono::duration_cast<boost::chrono::seconds>(d);
    if (seconds < d) {
        ++seconds;
    }

    return boost::chrono::seconds(10) + (seconds / 180);
}

inline boost::chrono::seconds
call_duration_to_delay(unsigned int seconds)
{
    return call_duration_to_delay(boost::chrono::seconds(seconds));
}

struct call_duration_based_delayed_producer
{
    typedef boost::chrono::seconds seconds;

    const common::collectd::fn_produce_type& produce_;
    const common::collectd::fn_produce_with_delay_type& produce_with_delay_;
    const seconds delay_;

    template <typename Rep, typename Period>
    static inline seconds
    calculate_delay(const std::time_t now, const std::time_t leg_end_epoch, const boost::chrono::duration<Rep, Period>& leg_duration_seconds)
    {
        seconds delay = call_duration_to_delay(leg_duration_seconds);

        if (now > leg_end_epoch) {
            delay -= seconds(now - leg_end_epoch);
        }

        return delay;
    }

    template <typename Rep, typename Period>
    static inline seconds
    calculate_delay(const std::time_t leg_end_epoch, const boost::chrono::duration<Rep, Period>& leg_duration_seconds)
    {
        return calculate_delay(std::time(0), leg_end_epoch, leg_duration_seconds);
    }

    template <typename Rep, typename Period>
    call_duration_based_delayed_producer(const common::collectd::fn_produce_type& fn_produce,
                                         const common::collectd::fn_produce_with_delay_type& fn_produce_with_delay,
                                         const std::time_t leg_end_epoch,
                                         const boost::chrono::duration<Rep, Period>& leg_duration_seconds)
        : produce_(fn_produce)
        , produce_with_delay_(fn_produce_with_delay)
        , delay_(calculate_delay(leg_end_epoch, leg_duration_seconds))
    {
    }

    inline void produce(const boost::shared_ptr<archiving::archivable>& item, bool add_to_catalogue = true) const
    {
        if (must_delay()) {
            produce_with_delay_(item, delay_, add_to_catalogue);
        } else {
            produce_(item, add_to_catalogue);
        }
    }

    inline bool must_delay() const
    {
        return delay_ > seconds::zero();
    }
};

}

#endif //COLLECTD_ARCHIVABLE_UTILS_HPP
