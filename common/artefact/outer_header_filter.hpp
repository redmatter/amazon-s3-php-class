//
// Created by dkorah on 14/09/22.
//

#ifndef ARCHIVING_OUTER_HEADER_FILTER_HPP
#define ARCHIVING_OUTER_HEADER_FILTER_HPP

#include "../../archived/file_format/headers.hpp"
#include "../../archived/types.hpp"
#include "detail/decode_options.hpp"

#include <boost/iostreams/concepts.hpp>

namespace common {
namespace artefact {

/**
 * outer_header_filter is a Boost.IOStreams output_filter that decodes what is written to it.
 */
struct outer_header_filter : public boost::iostreams::multichar_output_filter
{
    typedef char char_type;
    typedef boost::iostreams::multichar_output_filter_tag category;

    typedef archiving::archived::file_format::crypto_header_union header_type;

    header_type header;
    int header_attempts;
    bool expect_header_magic;
    bool expect_header;

    detail::decode_options& options;

    explicit outer_header_filter(detail::decode_options& options)
        : header(), header_attempts(18), expect_header_magic(true), expect_header(true), options(options)
    {
    }

    static inline bool is_magic(const uint8_t* const buf, std::size_t size)
    {
        return std::memcmp(buf, archiving::archived::file_format::crypto_header_magic_string(), size) == 0;
    }

    template<typename Sink>
    std::streamsize write(Sink& sink, const char_type* s, std::streamsize n)
    {
        if (!expect_header) {
            return boost::iostreams::write(sink, s, n);
        }

        if (expect_header_magic) {
            // wait for the "magic" bytes
            if (n < sizeof(header.magic) && header_attempts > 0) {
                header_attempts--;
                return 0;
            }

            // if we did not get a header, after many attempts, we have a problem
            if (header_attempts == 0) {
                throw std::runtime_error("Could not obtain magic header bytes");
            }

            expect_header_magic = false;

            // if the first bytes were not the magic bytes, we don't need to read a header
            if (!is_magic((const uint8_t*)(s), sizeof(header.magic))) {
                expect_header = false;
                return boost::iostreams::write(sink, s, n);
            }
        }

        if (expect_header) {
            // wait for the whole header to reduce complexity in code
            if (n < sizeof(header) && header_attempts > 0) {
                header_attempts--;
                return 0;
            }

            // if we did not get a header, after many attempts, we have a problem
            if (header_attempts == 0) {
                throw std::runtime_error("Could not obtain outer header bytes");
            }

            expect_header = false;

            std::memcpy(header.buffer, s, sizeof(header));
            options.set_outer_header(header);

            return sizeof(header);
        }

        // we will never reach here; just to satisfy the compiler
        return 0;
    }
};

} // namespace artefact
} // namespace common

#endif // ARCHIVING_OUTER_HEADER_FILTER_HPP
