//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 29/08/2018.
//

#ifndef ARCHIVING_PURGE_RESTRICTIONS_HPP
#define ARCHIVING_PURGE_RESTRICTIONS_HPP

#include "../../common/types.hpp"

#include <string>
#include <boost/program_options/variables_map.hpp>
#include <rml/exception.h>
#include <rml/logger.h>

namespace archiving
{
namespace archived
{

namespace detail
{
inline
std::string
get_criteria_from_config(const boost::program_options::variables_map& c)
{
    std::string where;

    if (c.empty()) {
        throw std::runtime_error("Empty configuration");
    }

    std::string where_enabled_filetypes;
    {
        const std::string& config(c["retention_purge.enabled_filetypes"].as<std::string>());
        if (config.empty()) {
            throw std::runtime_error("No filetypes enabled for purge");
        }

        if (config != "all") {
            std::vector<archiving::file_type::file_type_t> conf_types;
            std::ostringstream conf_ostr;
            {
                std::string tok;
                std::istringstream istr(config);
                while (std::getline(istr, tok, ','))
                    if (!tok.empty()) {
                        conf_types.push_back(archiving::file_type::from_string(tok));
                    }

                const std::vector<archiving::file_type::file_type_t>::const_iterator& begin(conf_types.begin());
                const std::vector<archiving::file_type::file_type_t>::const_iterator& end(conf_types.end());
                for (std::vector<archiving::file_type::file_type_t>::const_iterator itr = begin; itr != end; ++itr)
                    conf_ostr << ((itr == begin) ? ' ' : ',') << (int) *itr;
            }

            if (conf_types.size() == 1) {
                // not a list
                where_enabled_filetypes = "`r`.`Type` = " + conf_ostr.str();
            } else {
                // a list of file-types
                where_enabled_filetypes = "`r`.`Type` IN ( " + conf_ostr.str() + " )";
            }
        }
    }

    std::string where_enabled_orgs;
    {
        const std::string& enabled_orgs(c["retention_purge.enabled_orgs"].as<std::string>());
        if (enabled_orgs.empty()) {
            throw std::runtime_error("No Orgs enabled for purge");
        }

        if (enabled_orgs != "all") {
            if (std::string::npos == enabled_orgs.find(',')) {
                // not a list
                where_enabled_orgs = "`r`.`OrgID` = " + enabled_orgs;
            } else {
                // a list of org ids
                where_enabled_orgs = "`r`.`OrgID` IN ( " + enabled_orgs + " )";
            }
        }
    }

    std::string where_creation_date_range;
    {
        const std::string& creation_date_start(c["retention_purge.creation_date_range_start"].as<std::string>());
        const std::string& creation_date_end(c["retention_purge.creation_date_range_end"].as<std::string>());

        std::string where_start, where_end;

        if (!creation_date_start.empty()) {
            where_start = std::string("`r`.`CreationDate` >= '") + creation_date_start + "' ";
        }
        if (!creation_date_end.empty()) {
            where_end = std::string("`r`.`CreationDate` <= '") + creation_date_end + "' ";
        }

        if (!where_start.empty() && !where_end.empty()) {
            where_creation_date_range = std::string("( ") + where_start + "AND " + where_end + ") ";
        } else if (!where_start.empty()) {
            where_creation_date_range = where_start;
        } else if (!where_end.empty()) {
            where_creation_date_range = where_end;
        }
    }

    std::string where_lag_days;
    {
        const unsigned int& lag_days(c["retention_purge.lag_days"].as<unsigned int>());
        if (lag_days) {
            std::ostringstream ostr;
            ostr << "`r`.`RetentionMaxDate` < DATE_SUB(CURDATE(), INTERVAL " << lag_days << " DAY)";

            where_lag_days = ostr.str();
        }
    }

    if (!where_enabled_filetypes.empty()) {
        where += " AND " + where_enabled_filetypes;
    }
    if (!where_enabled_orgs.empty()) {
        where += " AND " + where_enabled_orgs;
    }
    if (!where_creation_date_range.empty()) {
        where += " AND " + where_creation_date_range;
    }
    if (!where_lag_days.empty()) {
        where += " AND " + where_lag_days;
    }

    return where;
}
} // namespace detail

struct purge_restrictions
{
    const bool disable_id_range_scan;
    const std::string criteria;

    inline static
    void describe_config(boost::program_options::options_description& desc)
    {
        desc.add_options()
                ("retention_purge.lag_days", boost::program_options::value<unsigned>()->default_value(7),
                 "purge date will lag by this many days")
                ("retention_purge.creation_date_range_start",
                 boost::program_options::value<std::string>()->default_value(""),
                 "purge files created within this range")
                ("retention_purge.creation_date_range_end",
                 boost::program_options::value<std::string>()->default_value(""),
                 "purge files created within this range")
                ("retention_purge.enabled_orgs", boost::program_options::value<std::string>()->default_value(""),
                 "purge enabled orgs")
                ("retention_purge.enabled_filetypes", boost::program_options::value<std::string>()->default_value(""),
                 "purge enabled filetypes")
                ("retention_purge.disable_id_range_scan",
                 boost::program_options::value<std::string>()->default_value("false"),
                 "Disable ID based batch scan (QA)");
    }

    explicit purge_restrictions(const boost::program_options::variables_map& config) :
            disable_id_range_scan(config["retention_purge.disable_id_range_scan"].as<std::string>() == "true"),
            criteria(detail::get_criteria_from_config(config))
    {
        if (disable_id_range_scan) {
            LogMessage("Purge: ID Range based scanning is disabled");
        }

        LogDebug("purge-restrictions criteria: " << criteria);
    }
};

} // namespace archived
} // namespace archiving

#endif //ARCHIVING_PURGE_RESTRICTIONS_HPP
