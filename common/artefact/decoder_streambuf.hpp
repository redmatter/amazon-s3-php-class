//
// Created by dkorah on 20/09/22.
//

#ifndef ARCHIVING_DECODER_STREAMBUF_HPP
#define ARCHIVING_DECODER_STREAMBUF_HPP

#include "detail/decode_options.hpp"
#include "decompress_filter.hpp"
#include "decrypter_filter.hpp"
#include "inner_header_filter.hpp"
#include "outer_header_filter.hpp"
#include "sanity_checker_filter.hpp"

#include <boost/iostreams/filtering_streambuf.hpp>

namespace common {
namespace artefact {

struct decoder_streambuf : public boost::iostreams::filtering_ostreambuf
{
    detail::decode_options options;

    template<typename Sink>
    decoder_streambuf(const key_type& k, Sink& sink) : options(k)
    {
        push(outer_header_filter(options), 8 * 1024);
        push(decrypter_filter(options), 8 * 1024);
        push(inner_header_filter(options), 8 * 1024);
        push(decompress_filter(options), 12 * 1024);
        push(sanity_checker_filter(options), 12 * 1024);

        push(sink, 12 * 1024);
    }
};

} // namespace artefact
} // namespace common

#endif // ARCHIVING_DECODER_STREAMBUF_HPP
