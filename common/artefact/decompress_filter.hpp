//
// Created by dkorah on 16/09/22.
//

#ifndef ARCHIVING_DECOMPRESS_FILTER_HPP
#define ARCHIVING_DECOMPRESS_FILTER_HPP

#include "detail/decode_options.hpp"
#include "detail/bzip2.hpp"

#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/filter/bzip2.hpp>

namespace common {
namespace artefact {

template<typename Alloc = std::allocator<char> >
struct basic_bzip2_decompressor : boost::iostreams::symmetric_filter<detail::bzip2_decompressor_impl<Alloc>, Alloc>
{
private:
    typedef detail::bzip2_decompressor_impl<Alloc> impl_type;
    typedef boost::iostreams::symmetric_filter<impl_type, Alloc> base_type;

public:
    typedef typename base_type::char_type char_type;
    typedef typename base_type::category category;
    explicit basic_bzip2_decompressor(detail::decode_options& options,
                                      int buffer_size = boost::iostreams::default_device_buffer_size)
        : base_type(buffer_size, options)
    {
    }
};
BOOST_IOSTREAMS_PIPABLE(basic_bzip2_decompressor, 1)

typedef basic_bzip2_decompressor<> decompress_filter;

} // namespace artefact
} // namespace common

#endif // ARCHIVING_DECOMPRESS_FILTER_HPP
