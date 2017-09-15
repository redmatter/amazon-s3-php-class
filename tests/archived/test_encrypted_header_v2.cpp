//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 12/09/17.
//

#include "gtest/gtest.h"
#include <boost/mpl/assert.hpp>
#include <boost/type_traits/is_same.hpp>

#include "../../archived/file_format/headers.hpp"
#include "../../archived/file_format/detail/parameter.hpp"
#include "../../archived/types.hpp"

TEST(FileFormatDetailParameter, Basics)
{
    namespace ns = archiving::archived::file_format::detail;
    namespace mpl = boost::mpl;

    {
        typedef ns::encryption_algorithm<1> enc_alg;
        BOOST_MPL_ASSERT((boost::is_same<enc_alg::type, CryptoPP::Blowfish>));
        BOOST_MPL_ASSERT_RELATION(enc_alg::iv_length::value, ==, CryptoPP::Blowfish::BLOCKSIZE);
        BOOST_MPL_ASSERT_RELATION(enc_alg::pad_length::value, ==, 8);
    }

    {
        typedef ns::encryption_algorithm<2> enc_alg;
        BOOST_MPL_ASSERT((boost::is_same<enc_alg::type, CryptoPP::AES>));
        BOOST_MPL_ASSERT_RELATION(enc_alg::iv_length::value, ==, CryptoPP::AES::BLOCKSIZE);
        BOOST_MPL_ASSERT_RELATION(enc_alg::pad_length::value, ==, 0);
    }

    using namespace archiving::archived::file_format;

    typedef crypto_header<version<1> > crypto_header_v1;
    typedef crypto_header<version<2> > crypto_header_v2;

    ASSERT_EQ(sizeof(crypto_header_v1::padding), crypto_header_v1::padding_length::value);
    ASSERT_EQ(sizeof(crypto_header_v2::padding), crypto_header_v2::padding_length::value);

    crypto_header_v1 header_v1;
    initialise_crypto_header(header_v1, version_number);
    std::memset(header_v1.iv, 0, sizeof(crypto_header_v1::iv));

    app::types::header_encrypted legacy_header;
    std::memset(legacy_header.iv, 0, sizeof(app::types::header_encrypted::iv));

    ASSERT_EQ(sizeof(crypto_header_v1), sizeof(app::types::header_encrypted));
    ASSERT_EQ(0, std::memcmp(&legacy_header, &header_v1, sizeof(crypto_header_v1)));

    ASSERT_EQ(sizeof(crypto_header_union), sizeof(app::types::header_encrypted));

    crypto_header_union header_union;
    std::memcpy(&header_union, &header_v1, sizeof(header_union));

    ASSERT_EQ(header_union.format_version.major, header_v1.format_version_major);

    std::memcpy(&legacy_header, &header_union, sizeof(legacy_header));
    ASSERT_EQ(legacy_header.format_version_major, header_union.format_version.major);

    ASSERT_EQ(0, std::memcmp(header_union.buffer, &legacy_header, sizeof(header_union.buffer)));
}
