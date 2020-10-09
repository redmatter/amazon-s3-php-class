//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 29/09/2020.
//

#ifndef ARCHIVING_VERIFY_CHAR_HPP
#define ARCHIVING_VERIFY_CHAR_HPP

#include <iostream>

namespace common {
namespace utils {

template<typename Char>
struct verify_char
{
    const Char c;
    explicit verify_char(Char c) : c(c) {}
};

template<typename Char>
inline std::istream&
operator>>(std::basic_istream<Char>& is, const verify_char<Char>& wss)
{
    if (wss.c != is.get()) {
        is.setstate(std::ios::failbit);
    }

    return is;
}

} // namespace utils
} // namespace common
#endif // ARCHIVING_VERIFY_CHAR_HPP
