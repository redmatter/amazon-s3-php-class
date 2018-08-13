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
#include "../common/basic_database.hpp"
#include "../common/coreapi_eventlog.hpp"

#include <rml/database/mysql/basic_database.hpp>
#include <rml/database/mysql/errors.hpp>
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

class retention_delete : public rmldb::basic_database<retention_delete>
{
public:
    static inline const char* get_identifier()
    {
        return "retention_delete";
    }

    explicit retention_delete(const boost::program_options::variables_map& confMap) :
            rmldb::basic_database<retention_delete>(confMap)
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
            boost::shared_ptr<rmldb::connection> conn = get_connection_wait_for(boost::chrono::seconds(5));
            mysqlpp::Transaction transaction(*conn, false);

            try {
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
                        query << "DELETE FROM `ArchivingRetention`.`Audit_"<<org<<"` "
                            "WHERE Type="<<int(id.filetype)<<" /* "<<id.filetype<<" */"
                            " AND UUID=0x"<<uuid_hex<<" /* "<<id.uuid<<" */";
                        PURGE_SQL_DEBUG(conn, query.str());
                        query.execute();
                    }

                    {
                        mysqlpp::Query query = conn->query();
                        query << "DELETE FROM `ArchivingRetention`.`Retention_"<<org<<"` "
                            "WHERE Type="<<int(id.filetype)<<" /* "<<id.filetype<<" */"
                            " AND UUID=0x"<<uuid_hex<<" /* "<<id.uuid<<" */";
                        PURGE_SQL_DEBUG(conn, query.str());
                        query.execute();
                    }
                }

                transaction.commit();

                return true;
            }
            catch (const mysqlpp::BadQuery& e) {
                LogError("MySQL Error: "<<e.what());
                abandon_connection_if_faulty(conn, transaction, e);
            }
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
