//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 10/01/2018.
//

#ifndef ARCHIVING_QUERY_MAP_HPP
#define ARCHIVING_QUERY_MAP_HPP

#include <string>

#include <strings.h>
#include <map>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>

namespace app
{
namespace http
{

namespace detail
{

struct caseless_string_compare
{
    bool operator()(const std::string& lhs, const std::string& rhs) const
    {
        return strcasecmp(lhs.c_str(), rhs.c_str()) < 0;
    }
};

typedef std::multimap<std::string, std::string, caseless_string_compare> base_map;

struct query_map : public base_map
{
    std::string get_first_value(const std::string& var, bool& found) const
    {
        const std::pair<base_map::const_iterator, base_map::const_iterator>& range(equal_range(var));
        if (range.first != range.second) {
            found = true;
            return range.first->second;
        }

        found = false;
        return std::string();
    }

    inline std::string get_first_value(const std::string& header) const
    {
        bool found;
        return get_first_value(header, found);
    }

    template<typename T>
    T get_first_value_as(const std::string& header, bool& found) const
    {
        const std::string& value(get_first_value(header, found));
        if (found) {
            return boost::lexical_cast<T>(value);
        }

        return T();
    }

    template<typename T>
    boost::optional<T> get_first_value_as(const std::string& param) const
    {
        bool found(false);
        const std::string& value_str(get_first_value(param, found));

        LogDebug("query-params - " << param << " = [" << value_str << ']');

        boost::optional<T> value;
        if (found) {
            value = lexical_cast<T>(value_str, param, "query-params");
        }

        return value;
    }
};

} // namespace detail
} // namespace http
} // namespace app

#endif //ARCHIVING_QUERY_MAP_HPP
