//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 02/10/2020.
//

#ifndef ARCHIVING_COMMON_UTILS_KBPS_HPP
#define ARCHIVING_COMMON_UTILS_KBPS_HPP

#include <boost/chrono/duration.hpp>

namespace common {
namespace utils {

template <class Rep, class Period>
double kbps(std::size_t bytes, const boost::chrono::duration<Rep, Period>& duration)
{
    return
        static_cast<double>(bytes * 8000) /
        static_cast<double>(boost::chrono::duration_cast<boost::chrono::microseconds>(duration).count());
}

} // namespace utils
} // namespace common

#endif // ARCHIVING_COMMON_UTILS_KBPS_HPP
