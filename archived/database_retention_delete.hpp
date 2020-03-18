//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 15/10/2017.
//

#ifndef DATABASE_RETENTION_DELETE_HPP_
#define DATABASE_RETENTION_DELETE_HPP_

#include "operation_data.hpp"
#include "types.hpp"
#include "retention_record.hpp"
#include "macros.h"

#include "../common/policy.hpp"
#include "../common/coreapi_eventlog.hpp"

#include <rml/database/mysql/transactional_connection.hpp>
#include <rml/database/mysql/basic_database.hpp>
#include <rml/database/mysql/errors.hpp>
#include <rml/database/mysql/utils.hpp>
#include <rml/exception.h>
#include <rml/logger.h>

#include <boost/program_options.hpp>
#include <boost/thread/thread.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/algorithm/string/erase.hpp>

#include <set>

#include <time.h>

namespace app {

extern std::string hostname;

namespace database {

namespace rmldb = rml::database::mysql;

class retention_delete : public rmldb::basic_database<retention_delete, rmldb::transactional_connection>
{
public:
    static inline const char* get_identifier()
    {
        return "retention_delete";
    }

    explicit retention_delete(const boost::program_options::variables_map& confMap) :
            rmldb::basic_database<retention_delete, rmldb::transactional_connection>(confMap)
    {}

    template<typename Iter, typename ExtractIdentifier, typename IsInternalFiletype>
    inline bool
    delete_records(const archiving::org_id_t &org, const Iter &begin, const Iter &end,
                   const ExtractIdentifier &extract_identifier, const IsInternalFiletype &is_internal_filetype)
    {
        if (begin == end) {
            LogWarning("Empty set given; ignored!");
            return true;
        }

        try {
            boost::shared_ptr<rmldb::transactional_connection> conn = get_connection_wait_for(boost::chrono::seconds(5));
            conn->start_transaction();

            for(Iter itr = begin; itr != end; ++itr) {
                const archiving::archivable::identifier& id(extract_identifier(*itr));
                if (!is_internal_filetype(id)) {
                    LogError("identifier:"<<id<<" - A non-internal-filetype was found in the batch to delete from retention-database");
                    return false;
                }

                std::string uuid_hex(id.uuid);
                boost::algorithm::erase_all(uuid_hex, "-");

                {
                    mysqlpp::Query query = conn->query();
                    query << "DELETE FROM `Audit_"<<org<<"` "
                          "WHERE Type="<<int(id.filetype)<<" /* "<<id.filetype<<" */"
                          " AND UUID="<<rmldb::utils::sql_uuid_value(id.uuid);
                    MYSQL_QUERY_EXECUTE(PURGE, conn, query);
                }

                {
                    mysqlpp::Query query = conn->query();
                    query << "DELETE FROM `Retention_"<<org<<"` "
                          "WHERE Type="<<int(id.filetype)<<" /* "<<id.filetype<<" */"
                          " AND UUID="<<rmldb::utils::sql_uuid_value(id.uuid);
                    MYSQL_QUERY_EXECUTE(PURGE, conn, query);
                }
            }

            conn->commit_transaction();

            return true;
        }
        MYSQL_CATCH_LOG_WARNING(e)

        LogWarning("Failed to delete from retention-database");

        return false;
    }
};

}  // namespace database

}  // namespace app

#endif /* DATABASE_RETENTION_DELETE_HPP_ */
