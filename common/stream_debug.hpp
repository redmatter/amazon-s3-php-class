//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 24/02/2020.
//

#ifndef ARCHIVING_STREAM_DEBUG_HPP
#define ARCHIVING_STREAM_DEBUG_HPP

#ifndef RML_DEV_DEBUG

#define make_istream(name, buf) \
	std::istream name(buf)

#define make_ostream(name, buf) \
	std::ostream name(buf)

#else // ifndef RML_DEV_DEBUG

#include "../common/utils/tee_stream.hpp"
#include <boost/preprocessor/cat.hpp>

#define make_istream(name, buf) \
	std::istream BOOST_PP_CAT(name, _std)(buf);
	common::utils::tee_istream name(BOOST_PP_CAT(name, _std), std::cerr)

#define make_ostream(name, buf) \
	std::ostream BOOST_PP_CAT(name, _std)(buf);
	common::utils::tee_ostream name(BOOST_PP_CAT(name, _std), std::cerr)

#endif // ifndef RML_DEV_DEBUG

#endif // ARCHIVING_STREAM_DEBUG_HPP
