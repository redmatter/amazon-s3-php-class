//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 12/09/2017.
//

#ifndef ARCHIVING_ARCHIVED_FILE_FORMAT_HEADERS_HPP
#define ARCHIVING_ARCHIVED_FILE_FORMAT_HEADERS_HPP

#include "detail/parameter.hpp"

#include <stdint.h>
#include <cstring>

#include <boost/mpl/int.hpp>
#include <boost/mpl/plus.hpp>
#include <boost/parameter/parameters.hpp>
#include <boost/preprocessor/repetition/repeat.hpp>

#define STRING_GEN(ignore1, ignore2, text)  text
#define STRING_REPEAT(str, n) BOOST_PP_REPEAT(n, STRING_GEN, str)

namespace archiving
{
namespace archived
{
namespace file_format
{

static inline const char *crypto_header_magic_string()
{
	return "RMA-CE";
}

namespace detail
{

static inline const char *base_padding_string()
{
    return STRING_REPEAT("RedMatter", 7);
}

typedef boost::parameter::parameters<
        boost::parameter::required<tag::version> > crypto_header_signature;

} // namespace detail

#pragma pack(push, 1)
template <typename Version>
struct crypto_header
{
    typedef typename detail::crypto_header_signature::bind<Version>::type bound_args;
    static const unsigned int version_specified = detail::extract_version<bound_args>::value;
    typedef typename detail::encryption_algorithm<version_specified> encryption_algorithm_type;
    typedef typename encryption_algorithm_type::iv_length iv_length;

    typedef boost::mpl::int_<13> reserved_bytes;
    typedef boost::mpl::plus<reserved_bytes, typename encryption_algorithm_type::pad_length> padding_length;

    uint8_t magic[6];

    uint8_t archived_version_major;
    uint8_t archived_version_minor;
    uint8_t archived_version_patch;

    uint8_t format_version_major;
    uint8_t format_version_minor;

    uint8_t iv[iv_length::value];
    uint8_t padding[padding_length::value];
};

union crypto_header_union
{
    BOOST_MPL_ASSERT_RELATION(sizeof(crypto_header<version<1> >), ==, sizeof(crypto_header<version<2> >));

    uint8_t buffer[sizeof(crypto_header<version<1> >)];

    struct version_type
    {
        uint8_t buf1[9];
        uint8_t major;
        uint8_t minor;
        uint8_t buf2[29];
    } format_version;

    uint8_t magic[6];

    crypto_header<version<1> > v1;
    crypto_header<version<2> > v2;
};
#pragma pack(pop)

template <typename Version>
void initialise_crypto_header(crypto_header<Version>& header, const int scm_version[4])
{
    typedef crypto_header<Version> crypto_header_type;

    std::memcpy(header.magic, crypto_header_magic_string(), sizeof(crypto_header_type::magic));
    header.archived_version_major = (uint8_t) scm_version[0];
    header.archived_version_minor = (uint8_t) scm_version[1];
    header.archived_version_patch = (uint8_t) scm_version[2];
    header.format_version_major = crypto_header_type::version_specified;
    header.format_version_minor = (uint8_t) 0U;
    std::memcpy(header.padding, detail::base_padding_string(), sizeof(crypto_header_type::padding));
}

template <typename Version>
bool check_crypto_header_sanity(const crypto_header<Version>& header)
{
    typedef crypto_header<Version> crypto_header_type;

    const int ignored_version[4] = {0, 0, 0, 0};

    crypto_header_type blank;
    initialise_crypto_header(blank, ignored_version);
    return std::memcmp(header.magic, blank.magic, 6) == 0 &&
            header.format_version_major == blank.format_version_major &&
            std::memcmp(header.padding, blank.padding, sizeof(crypto_header_type::padding)) == 0;
}

} // namespace file_format
} // namespace archived
} // namespace archiving

#endif //ARCHIVING_ARCHIVED_FILE_FORMAT_HEADERS_HPP
