//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 29/09/2020.
//

#ifndef ARCHIVING_DISABLE_STREAM_FLAG_HPP
#define ARCHIVING_DISABLE_STREAM_FLAG_HPP

#include <istream>

namespace common {
namespace utils {

struct disable_skipws
{
    explicit disable_skipws(std::istream& is) : is(is), was_enabled(is.flags() & std::ios::skipws)
    {
        is.unsetf(std::ios::skipws);
    }

    ~disable_skipws()
    {
        if (was_enabled) {
            is.setf(std::ios::skipws);
        }
    }

private:
    std::istream& is;
    bool was_enabled;
};

} // namespace utils
} // namespace common
#endif // ARCHIVING_DISABLE_STREAM_FLAG_HPP
