//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 02/10/2020.
//

#ifndef ARCHIVING_COMMON_UTILS_GETENV_HPP
#define ARCHIVING_COMMON_UTILS_GETENV_HPP

#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>

namespace common {
namespace utils {

template<typename T>
inline boost::optional<T>
getenv(const char* name)
{
    const char* str = std::getenv(name);
    if (!str) {
        return boost::none;
    }

    return boost::lexical_cast<T>(str);
}

template<>
inline boost::optional<const char *>
getenv<const char*>(const char* name)
{
    const char* str = std::getenv(name);
    if (!str) {
        return boost::none;
    }

    return str;
}

template<typename T>
inline T
getenv(const char* name, const T& default_value)
{
    const char* str = std::getenv(name);
    if (!str) {
        return default_value;
    }

    return boost::lexical_cast<T>(str);
}

inline const char*
getenv(const char* name, const char*& default_value)
{
    const char* str = std::getenv(name);
    if (!str) {
        return default_value;
    }

    return str;
}

template<typename T>
inline T
must_getenv(const char* name)
{
    boost::optional<T> v = getenv<T>(name);
    if (v == boost::none) {
        throw std::runtime_error(std::string(name) + " : environment variable not found");
    }

    return *v;
}

} // namespace utils
} // namespace common

#endif // ARCHIVING_COMMON_UTILS_GETENV_HPP
