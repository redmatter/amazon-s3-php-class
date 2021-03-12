//
// Created by dkorah on 04/07/17.
//

#ifndef COLLECTD_ARCHIVABLE_UTILS_HPP
#define COLLECTD_ARCHIVABLE_UTILS_HPP

#include "../../common/archivable.hpp"

namespace archiving {

std::size_t hash_value(archivable const &a)
{
    boost::hash<std::string> hasher;
    return hasher(a.get_uuid());
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

}

#endif //COLLECTD_ARCHIVABLE_UTILS_HPP
