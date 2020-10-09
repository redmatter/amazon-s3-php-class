//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 01/10/2020.
//

#ifndef ARCHIVING_STREAM_PRINTF_HPP
#define ARCHIVING_STREAM_PRINTF_HPP

#include <iostream>
#include <stdio.h>

namespace common {
namespace utils {

namespace detail {

template<typename T>
struct printf_impl
{
    const char* fmt;
    const T v;

    printf_impl(const char* fmt, const T& v) : fmt(fmt), v(v) {}
};

template<typename T>
inline typename boost::enable_if<boost::is_pod<T>, std::ostream&>::type
operator<<(std::ostream& os, const printf_impl<T>& p)
{
    char buf[40];
    ::snprintf(buf, sizeof(buf), p.fmt, p.v);
    return os << buf;
}

} // namespace detail

template<typename T>
inline typename boost::enable_if<boost::is_pod<T>, detail::printf_impl<T> >::type
printf(const char* fmt, const T& v)
{
    return detail::printf_impl<T>(fmt, v);
}

} // namespace utils
} // namespace common

#endif // ARCHIVING_STREAM_PRINTF_HPP
