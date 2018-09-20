//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 13/09/2017.
//

#ifndef ARCHIVING_ARCHIVED_FILE_FORMAT_DETAIL_PARAMETER_HPP
#define ARCHIVING_ARCHIVED_FILE_FORMAT_DETAIL_PARAMETER_HPP

#include "../policies.hpp"
#include <boost/mpl/greater_equal.hpp>
#include <boost/mpl/less_equal.hpp>
#include <boost/mpl/equal_to.hpp>
#include <boost/mpl/min_max.hpp>
#include <boost/mpl/minus.hpp>
#include <boost/mpl/size_t.hpp>
#include <cryptopp/aes.h>
#include <cryptopp/blowfish.h>
#include <cryptopp/modes.h>

namespace archiving
{
namespace archived
{
namespace file_format
{
namespace detail
{

namespace mpl = boost::mpl;

template <typename bound_args, typename tag_type>
struct has_arg
{
    typedef typename boost::parameter::binding<bound_args, tag_type, mpl::void_>::type type;
    static const bool value = mpl::is_not_void_<type>::type::value;
};

template <typename bound_args, unsigned default_ = 2>
struct extract_version
{
    static const bool has_version = has_arg<bound_args, tag::version>::value;

    typedef typename mpl::if_c<has_version,
            typename has_arg<bound_args, tag::version>::type,
            mpl::int_<default_>
    >::type type;

    static const unsigned int value = static_cast<const unsigned int>(type::value);
};

template<unsigned MajorVersion>
struct encryption_algorithm
{
    BOOST_MPL_ASSERT((boost::mpl::and_<
            boost::mpl::greater_equal<boost::mpl::int_<MajorVersion>, boost::mpl::int_<1> >,
            boost::mpl::less_equal<boost::mpl::int_<MajorVersion>, boost::mpl::int_<2> > >));

    typedef typename mpl::if_<
            mpl::equal_to<mpl::int_<MajorVersion>, mpl::int_<1> >, CryptoPP::Blowfish,
            typename mpl::if_<
                    mpl::equal_to<mpl::int_<MajorVersion>, mpl::int_<2> >, CryptoPP::AES,
                    void>::type
    >::type cipher;

    typedef typename mpl::if_<
            mpl::equal_to<mpl::int_<MajorVersion>, mpl::int_<1> >, CryptoPP::CBC_Mode<cipher>,
            typename mpl::if_<
                    mpl::equal_to<mpl::int_<MajorVersion>, mpl::int_<2> >, CryptoPP::CFB_Mode<cipher>,
                    void>::type
    >::type cipher_mode;

    typedef mpl::max<
            mpl::int_<CryptoPP::Blowfish::BLOCKSIZE>,
            mpl::int_<CryptoPP::AES::BLOCKSIZE>
    >::type max_iv_length;

    typedef mpl::size_t<cipher::BLOCKSIZE> iv_length;
    typedef boost::mpl::minus< max_iv_length, iv_length > pad_length;
};

} // namespace detail
} // namespace file_format
} // namespace archived
} // namespace archiving

#endif //ARCHIVING_ARCHIVED_FILE_FORMAT_DETAIL_PARAMETER_HPP
