//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 10/01/2018.
//

#ifndef ARCHIVING_PATH_INFO_HPP
#define ARCHIVING_PATH_INFO_HPP

#include <boost/optional.hpp>

#include <vector>
#include <string>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>

namespace app
{
namespace http
{
namespace detail
{
struct path_info : public std::vector<std::string>
{
    template <typename T>
    boost::optional<T> get_component(const unsigned pos) const
    {
        if (pos > size() || pos == 0) {
            return boost::none;
        }

        return boost::lexical_cast<T>(at(pos - 1));
    }

    path_info()
    {}

    explicit path_info(const std::string& path)
    {
        std::vector<std::string> tmp;
        boost::algorithm::split(tmp, path, boost::algorithm::is_any_of("/"), boost::token_compress_on);
        tmp.erase(std::remove_if(tmp.begin(), tmp.end(), boost::bind(&std::string::empty, _1)), tmp.end());

        swap(tmp);
    }
};
} // namespace detail
} // namespace http
} // namespace app
#endif //ARCHIVING_PATH_INFO_HPP
