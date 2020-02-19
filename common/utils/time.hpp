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
template<typename Clock>
inline boost::chrono::system_clock::time_point
to_system_clock(const boost::chrono::time_point<Clock>& t)
{
	return boost::chrono::system_clock::now() + (t - Clock::now());
}

template<>
inline boost::chrono::system_clock::time_point
to_system_clock<boost::chrono::system_clock>(const boost::chrono::time_point<boost::chrono::system_clock>& t)
{
	return t;
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
