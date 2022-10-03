//
// Created by dkorah on 20/09/22.
//

#ifndef ARCHIVING_DECODER_OSTREAM_HPP
#define ARCHIVING_DECODER_OSTREAM_HPP

#include "decompress_filter.hpp"
#include "decrypter_filter.hpp"
#include "detail/decode_options.hpp"
#include "inner_header_filter.hpp"
#include "outer_header_filter.hpp"
#include "sanity_checker_filter.hpp"

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/type_traits/is_integral.hpp>

namespace common {
namespace artefact {

template<typename T1, typename T2>
inline typename boost::enable_if<
    boost::mpl::and_<
        boost::is_convertible<T1, T2>,
        boost::is_integral<T1>,
        boost::is_integral<T2> >, T2>::type
multiple_of(T1 factor, T2 value)
{
    T2 r = value % factor;
    if (r == 0) {
        return value;
    }

    return value + factor - r;
}

struct decoder_ostream : public boost::iostreams::filtering_ostream
{
    detail::decode_options options;

    /**
     * Setup a filtering output stream wrapping the sink. It can decrypt using the key k and decompress, depending on
     * the file-format written to it. If the data written is neither decrypted nor compressed, then it will act like a
     * pass-through, with the overhead of copying through buffers.
     *
     * @tparam Sink
     * @param sink
     * @param k
     * @param buf_size
     */
    template<typename Sink>
    decoder_ostream(Sink& sink, const key_type& k, std::streamsize buf_size = 8192) : options(k)
    {
        buf_size = multiple_of(4096, buf_size);

        // BZip2 compressed WAV expands ~20% on decompression
        std::streamsize buf_size_expanded = multiple_of(4096, buf_size * 120 / 100);

        push(outer_header_filter(options), buf_size);
        push(decrypter_filter(options, static_cast<int>(buf_size)), buf_size);
        push(inner_header_filter(options), buf_size);
        push(decompress_filter(options, static_cast<int>(buf_size)), buf_size_expanded);
        push(sanity_checker_filter(options, static_cast<int>(buf_size_expanded)), buf_size_expanded);

        push(sink, buf_size_expanded);
    }
};

} // namespace artefact
} // namespace common

#endif // ARCHIVING_DECODER_OSTREAM_HPP
