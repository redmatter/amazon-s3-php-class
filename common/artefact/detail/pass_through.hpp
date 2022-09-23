//
// Created by dkorah on 21/09/22.
//

#ifndef ARCHIVING_PASS_THROUGH_HPP
#define ARCHIVING_PASS_THROUGH_HPP

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace common {
namespace artefact {
namespace detail {

inline bool
pass_through(const char*& begin_in, const char* end_in, char*& begin_out, const char* end_out, bool flush)
{
    (void)flush;

    // the size of buffers
    std::size_t in_length = end_in - begin_in;
    std::size_t out_length = end_out - begin_out;

    // we can't copy to a smaller buffer than we have
    std::size_t copy_length = std::min(out_length, in_length);

    // copy data
    std::memcpy(begin_out, begin_in, copy_length);

    // to tell the caller that you've consumed some data, you need to modify
    // begin_in to point to the next unconsumed byte (and so, it follows that consuming all
    // the input means that begin_in == end_in).
    begin_in += copy_length;

    // similar to begin_in, to tell the caller that you've written some data, you need
    // to modify begin_out to point to the next unwritten byte.
    begin_out += copy_length;

    // return true to indicate you have more data to write out before receiving some
    // more data, while returning false means you're ready for more input data
    return begin_in != end_in;
}

} // namespace detail
} // namespace artefact
} // namespace common

#endif // ARCHIVING_PASS_THROUGH_HPP
