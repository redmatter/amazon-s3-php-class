//
// Created by dkorah on 20/09/22.
//

#ifndef ARCHIVING_DETAIL_SANITY_CHECK_FILTER_HPP
#define ARCHIVING_DETAIL_SANITY_CHECK_FILTER_HPP

#include "pass_through.hpp"

#include <cryptopp/crc.h>
#include <cryptopp/filters.h>

#include <iostream>

#include <stdint.h>

namespace common {
namespace artefact {
namespace detail {

struct sanity_checker_impl
{
    typedef char char_type;

    typedef boost::function<void(std::streamsize, uint32_t)> result_callback_type;
    typedef boost::function<bool()> is_enabled_callback_type;

    bool initialised;
    bool enabled;
    bool eof;

    std::streamsize size;
    uint32_t crc32;

    CryptoPP::CRC32 hash;
    CryptoPP::HashFilter hash_filter;

    const is_enabled_callback_type is_enabled_callback;
    const result_callback_type result_callback;

    explicit sanity_checker_impl(const is_enabled_callback_type& is_enabled_callback,
                                 const result_callback_type& result_callback)
        : initialised(false)
        , enabled(false)
        , eof(false)
        , size(0)
        , crc32(0)
        , hash()
        , hash_filter(hash, 0)
        , is_enabled_callback(!is_enabled_callback.empty()
                                  ? is_enabled_callback
                                  : throw std::logic_error("is_enabled_callback cannot be empty"))
        , result_callback(!result_callback.empty() ? result_callback
                                                   : throw std::logic_error("result_callback cannot be empty"))
    {
    }

    bool filter(const char*& begin_in, const char* end_in, char*& begin_out, char* end_out, bool flush)
    {
        if (!initialised) {
            enabled = is_enabled_callback();
            initialised = true;
        }

        const char* begin_sent = begin_in;

        // this filter always pass through as much as possible and accumulates CRC32 etc on the data that passed through
        bool res = pass_through(begin_in, end_in, begin_out, end_out, flush);

        if (!enabled) {
            return res;
        }

        // The new begin_in is end_sent ... i.e. we have to hash the buffer [begin_sent-end_sent)
        const char* end_sent = begin_in;

        size += (end_sent - begin_sent);

        while (begin_sent != end_sent) {
            begin_sent = end_sent - hash_filter.Put(reinterpret_cast<const byte*>(begin_sent), end_sent - begin_sent);
        }

        if (flush) {
            if (begin_in == end_in && !eof) {
                hash_filter.MessageEnd();
                hash_filter.Flush(true);
                eof = true;
            }

            if (eof) {
                hash_filter.Get(reinterpret_cast<byte*>(&crc32), CryptoPP::CRC32::DIGESTSIZE);
                result_callback(size, crc32);

                return false;
            }
        }

        return res;
    }

    void close() {}
};

} // namespace detail
} // namespace artefact
} // namespace common

#endif // ARCHIVING_DETAIL_SANITY_CHECK_FILTER_HPP
