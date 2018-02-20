//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 04/12/2017.
//

#ifndef CDRMUNCH_HURLER_APP_CONFIG_COMMON_HPP
#define CDRMUNCH_HURLER_APP_CONFIG_COMMON_HPP

#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/options_description.hpp>

#include <string>
#include <boost/thread.hpp>

namespace app
{
namespace config
{
struct common
{
    const std::string cdrmunch_cache_dir;

    explicit common(const boost::program_options::variables_map& config_map) :
            cdrmunch_cache_dir(config_map["common.cdrmunch-cache-dir"].as<std::string>())
    {}

    static inline void describe_config(boost::program_options::options_description& options_desc)
    {
        using namespace boost::program_options;

        options_desc.add_options()
                ("common.io_service_threads",
                 value<unsigned>()->default_value(boost::thread::hardware_concurrency()),
                 "Number of io_service threads")
                ("common.cdrmunch-cache-dir",
                 value<std::string>()->default_value(std::string("/var/spool/cdrtmp/cdrmunch-cache")),
                 "cdrmunch shared cache folder (aka cdr-cache)");
    }
};
} // namespace config
} // namespace app
#endif //CDRMUNCH_HURLER_APP_CONFIG_COMMON_HPP
