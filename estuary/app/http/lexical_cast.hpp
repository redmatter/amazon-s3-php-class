//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 16/01/2018.
//

#ifndef ARCHIVING_LEXICAL_CAST_HPP
#define ARCHIVING_LEXICAL_CAST_HPP

#include <boost/lexical_cast.hpp>
#include "../error.hpp"

namespace app
{
namespace http
{

template<typename Target, typename Source>
inline Target lexical_cast(const Source& arg, const std::string& what, const std::string& where)
{
    try {
        return boost::lexical_cast<Target>(arg);
    } catch (const boost::bad_lexical_cast&) {
        throw error::bad_data_exception(what, where);
    }
}

} // namespace http
} // namespace app
#endif //ARCHIVING_LEXICAL_CAST_HPP
