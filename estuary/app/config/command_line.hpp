//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 06/12/2017.
//

#ifndef CDRMUNCH_HURLER_APP_CONFIG_COMMAND_LINE_HPP
#define CDRMUNCH_HURLER_APP_CONFIG_COMMAND_LINE_HPP

#include "../../defines.h"

#include <rml/version.h>

#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/format.hpp>

#include <string>

namespace app
{
namespace config
{

namespace bpo = boost::program_options;

struct command_line
{
    bpo::variables_map config_map;
    bpo::options_description options_desc;

    explicit command_line(int argc, const char* argv[]) :
            options_desc(boost::str(
                    boost::format(DEFAULT_APP_LONG_NAME" v%1%\n%2%\n\nOptions")
                    % version_number_string % copyright_string))
    {
        options_desc.add_options()
                ("conf", bpo::value<std::string>()->default_value(std::string(CONFIG_FILE_PATH)),
                 "alternate location of conf file ( default: "CONFIG_FILE_PATH" )")
                ("pid", bpo::value<std::string>()->default_value(std::string(PID_FILE_PATH)),
                 "alternate location of pid file ( default: "PID_FILE_PATH" )")
                ("help", "this help")
                ("help-conf", "config file help")
                ("version", "shows version information");

        bpo::store(bpo::parse_command_line(argc, argv, options_desc), config_map);
        bpo::notify(config_map);
    }

    template<typename T>
    inline T get_option(const char* opt)
    {
        return config_map[opt].as<T>();
    }

    inline bool is_opted(const char* opt)
    {
        return config_map.count(opt) > 0;
    }

    inline void display_help(std::ostream& os)
    {
        os << options_desc << '\n';
    }
};
} // namespace config
} // namespace app
#endif //CDRMUNCH_HURLER_APP_CONFIG_COMMAND_LINE_HPP
