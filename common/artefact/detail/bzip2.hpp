//
// Created by dkorah on 20/09/22.
//

#ifndef ARCHIVING_BZIP2_HPP
#define ARCHIVING_BZIP2_HPP

#include "decode_options.hpp"
#include "pass_through.hpp"

#include <boost/iostreams/filter/bzip2.hpp>


namespace common {
namespace artefact {
namespace detail {

/**
 * Extends the default bzip2 decompressor to bypass decompression when the file-format doesnt have compression.
 * @tparam Alloc
 */
template<typename Alloc = std::allocator<char> >
struct bzip2_decompressor_impl : public boost::iostreams::detail::bzip2_decompressor_impl<Alloc>
{
    typedef boost::iostreams::detail::bzip2_decompressor_impl<Alloc> base_type;

    const decode_options& options;

    bzip2_decompressor_impl(const decode_options& options) : base_type(false), options(options) {}

    bool filter(const char*& begin_in, const char* end_in, char*& begin_out, char* end_out, bool flush)
    {
        if (options.is_compressed()) {
            return base_type::filter(begin_in, end_in, begin_out, end_out, flush);
        }

        return pass_through(begin_in, end_in, begin_out, end_out, flush);
    }

    void close()
    {
        if (options.is_compressed()) {
            base_type::close();
            return;
        }
    }
};

} // namespace detail
} // namespace artefact
} // namespace common

#endif // ARCHIVING_BZIP2_HPP
