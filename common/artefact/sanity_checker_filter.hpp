//
// Created by dkorah on 20/09/22.
//

#ifndef ARCHIVING_SANITY_CHECKER_FILTER_HPP
#define ARCHIVING_SANITY_CHECKER_FILTER_HPP

#include "detail/decode_options.hpp"
#include "detail/sanity_checker.hpp"

#include <boost/iostreams/concepts.hpp>
#include <boost/make_shared.hpp>

namespace common {
namespace artefact {

template<typename Alloc = std::allocator<char> >
struct basic_sanity_checker_filter : boost::iostreams::symmetric_filter<detail::sanity_checker_impl, Alloc>
{
private:
    typedef basic_sanity_checker_filter<Alloc> self;

    typedef detail::sanity_checker_impl impl_type;
    typedef boost::iostreams::symmetric_filter<impl_type, Alloc> base_type;

    static void check(detail::decode_options& options, uint64_t size, uint32_t crc32)
    {
        if (!options.has_inner_header()) {
            return;
        }

        options.set_sanity_check_result(size == options.inner_header->inflated_size &&
                                        crc32 == options.inner_header->crc32);
    }

public:
    typedef typename base_type::char_type char_type;
    typedef typename base_type::category category;
    explicit basic_sanity_checker_filter(detail::decode_options& options,
                                         int buffer_size = boost::iostreams::default_device_buffer_size)
        : base_type(buffer_size,
                    boost::bind(&detail::decode_options::has_inner_header, boost::ref(options)),
                    boost::bind(check, boost::ref(options), _1, _2))
    {
    }
};
BOOST_IOSTREAMS_PIPABLE(basic_sanity_checker_filter, 1)

typedef basic_sanity_checker_filter<> sanity_checker_filter;

} // namespace artefact
} // namespace common

#endif // ARCHIVING_SANITY_CHECKER_FILTER_HPP
