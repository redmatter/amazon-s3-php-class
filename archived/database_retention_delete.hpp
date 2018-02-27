//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
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

#include "../common/policy.hpp"
#include "../common/basic_database.hpp"
#include "../common/coreapi_eventlog.hpp"

#include <rml/DBConnectionPool.h>
#include <rml/exception.h>
#include <rml/logger.h>

#include <mysql/mysqld_error.h>

#include <boost/program_options.hpp>
#include <boost/thread/thread.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/algorithm/string/erase.hpp>

#include <set>

#include <time.h>

#ifdef RETENTION_PURGE_SQL_DEBUG
#define __SQL_DEBUG(line) \
    SQL_DEBUG_IF(true, line)
#else
#define __SQL_DEBUG(line) \
    SQL_DEBUG(line)
#endif

namespace app {

extern std::string hostname;

namespace database {

class retention_delete : public basic_database< retention_delete >
{
public:
    static inline const char* get_identifier()
    {
        static const char* identifier = "retention_delete";
        return identifier;
    }

    explicit retention_delete(const boost::program_options::variables_map &confMap) : basic_database<retention_delete>(confMap)
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

        try
        {
            rml::MySQLConnection conn(*p_pool_);
            mysqlpp::Transaction transaction(*conn, false);

            for(Iter itr = begin; itr != end; ++itr)
            {
                const archiving::archivable::identifier& id(extract_identifier(*itr));
                if (!is_internal_filetype(id)) {
                    LogError("identifier:"<<id<<" - A non-internal-filetype was found in the batch to delete from retention-database");
                    return false;
                }

                std::string uuid_hex(id.uuid);
                boost::algorithm::erase_all(uuid_hex, "-");

                {
                    mysqlpp::Query query = conn->query();
                    query << "DELETE FROM `ArchivingRetention`.`Audit_"<<org<<"` "
                            "WHERE Type="<<int(id.filetype)<<" /* "<<id.filetype<<" */"
                            " AND UUID=0x"<<uuid_hex<<" /* "<<id.uuid<<" */";
                    __SQL_DEBUG(query.str());
                    query.execute();
                }

                {
                    mysqlpp::Query query = conn->query();
                    query << "DELETE FROM `ArchivingRetention`.`Retention_"<<org<<"` "
                            "WHERE Type="<<int(id.filetype)<<" /* "<<id.filetype<<" */"
                            " AND UUID=0x"<<uuid_hex<<" /* "<<id.uuid<<" */";
                    __SQL_DEBUG(query.str());
                    query.execute();
                }
            }

            transaction.commit();

            return true;
        }
        catch(const std::exception& e) {
            LogWarning("Failed to delete from retention-database - err:"<<e.what());
        }

        return false;
    }
};

}  // namespace database

}  // namespace app

#endif /* DATABASE_RETENTION_DELETE_HPP_ */
