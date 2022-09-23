//
// Created by dkorah on 16/09/22.
//

#ifndef ARCHIVING_DECRYPTER_FILTER_HPP
#define ARCHIVING_DECRYPTER_FILTER_HPP

#include "detail/decode_options.hpp"
#include "detail/decrypter.hpp"

#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/filter/bzip2.hpp>

namespace common {
namespace artefact {

template<typename Alloc = std::allocator<char> >
struct basic_decrypter_filter : boost::iostreams::symmetric_filter<detail::decrypter_impl, Alloc>
{
private:
    typedef detail::decrypter_impl impl_type;
    typedef boost::iostreams::symmetric_filter<impl_type, Alloc> base_type;

public:
    typedef typename base_type::char_type char_type;
    typedef typename base_type::category category;
    explicit basic_decrypter_filter(detail::decode_options& options,
                                      int buffer_size = boost::iostreams::default_device_buffer_size)
        : base_type(buffer_size, options)
    {
    }
};
BOOST_IOSTREAMS_PIPABLE(basic_decrypter_filter, 1)

typedef basic_decrypter_filter<> decrypter_filter;

} // namespace artefact
} // namespace common

#endif // ARCHIVING_DECRYPTER_FILTER_HPP
