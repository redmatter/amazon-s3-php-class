//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 19/02/2020.
//

#ifndef ARCHIVING_ARCHIVED_UTILS_TIME_HPP
#define ARCHIVING_ARCHIVED_UTILS_TIME_HPP

#include <boost/chrono/chrono.hpp>

namespace common {
namespace utils {

namespace detail {

template<typename ClockTo, typename ClockFrom>
inline typename boost::enable_if<typename boost::is_same<ClockTo, ClockFrom>, boost::chrono::time_point<ClockTo> >::type
to_clock(const boost::chrono::time_point<ClockTo>& t)
{
	return t;
}

template<typename ClockTo, typename ClockFrom>
inline typename boost::disable_if<typename boost::is_same<ClockTo, ClockFrom>, boost::chrono::time_point<ClockTo> >::type
to_clock(const boost::chrono::time_point<ClockFrom>& t)
{
	return ClockTo::now() + (t - ClockFrom::now());
}

} // namespace detail

template<typename Clock>
inline boost::chrono::system_clock::time_point
to_system_clock(const boost::chrono::time_point<Clock>& t)
{
	return detail::to_clock<boost::chrono::system_clock, Clock>(t);
}

inline boost::chrono::system_clock::time_point
to_system_clock(const std::time_t& t)
{
	return boost::chrono::system_clock::from_time_t(t);
}

template<typename Clock>
inline boost::chrono::steady_clock::time_point
to_steady_clock(const boost::chrono::time_point<Clock>& t)
{
	return detail::to_clock<boost::chrono::steady_clock, Clock>(t);
}

inline boost::chrono::steady_clock::time_point
to_steady_clock(const std::time_t& t)
{
	return detail::to_clock<boost::chrono::steady_clock, boost::chrono::system_clock>(to_system_clock(t));
}

template<typename Clock>
inline std::time_t
to_time_t(const boost::chrono::time_point<Clock>& t)
{
	return boost::chrono::system_clock::to_time_t(to_system_clock(t));
}
} // namespace utils
} // namespace archiving
#endif // ARCHIVING_ARCHIVED_UTILS_TIME_HPP
