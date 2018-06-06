//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 07/03/2011.
//

#ifndef CDRMUNCH_DATABASE_HPP_
#define CDRMUNCH_DATABASE_HPP_

#include "whisper_client.hpp"
#include "macros.h"

#include <rml/database/mysql/basic_database.hpp>
#include <rml/database/mysql/errors.hpp>

#include "../common/basic_database.hpp"

#include <rml/DBConnectionPool.h>
#include <rml/exception.h>
#include <rml/logger.h>

#include <mysql/mysqld_error.h>

#include <boost/program_options.hpp>
#include <boost/thread/thread.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <set>

#include <time.h>

namespace app {

namespace database {

namespace rmldb = rml::database::mysql;

class cdrmunch : public rmldb::basic_database<cdrmunch>
{
public:
	static inline const char* get_identifier()
	{
		return "cdrmunch";
	}

	explicit cdrmunch(const boost::program_options::variables_map& confMap) :
		rmldb::basic_database<cdrmunch>(confMap)
	{
	}

	bool save(whisper::pointer p_whisper)
	{
		try {
			boost::shared_ptr<rmldb::connection> conn = pool_->grab();
            try {
                mysqlpp::Query query = conn->query();
                query << "INSERT IGNORE INTO cdr_queue (path, create_time, next_attempt, retry, fs_host, uuid)"
                         " VALUES "
                         "( %0q:path, FROM_UNIXTIME( %1:create_time ), NOW(), 0, %2q:fs_host, UNHEX( REPLACE( %3q:uuid, '-', '' ) ) )";
                query.parse();

                mysqlpp::SQLQueryParms qp;
                qp << p_whisper->cdr_path << p_whisper->create_time << p_whisper->fs_host << p_whisper->uuid;

                SqlDebug(conn, query.str(qp));
                query.execute(qp);

                return true;
            }
            catch (const mysqlpp::BadQuery& e) {
                LogError("MySQL Error: "<<e.what());
                if (rml::database::mysql::errors::is_connection_invalidated_by_exception(e)) {
                    // just in case we get the error classification wrong!
                    // make sure we dont get another exception from any of the operations below
                    conn->disable_exceptions();
                    conn->disconnect();
                    pool_->release_faulty(conn);
                }
            }
		}
		catch(const mysqlpp::Exception& e) {
			LogError("MySQL Exception: "<<e.what());
		}
		catch(const std::exception& e) {
			LogError("Exception: "<<e.what());
		}
		catch(...) {
			LogError("Unknown Exception");
		}

		return false;
	}
};

}  // namespace database

}  // namespace app

#endif /* CDRMUNCH_DATABASE_HPP_ */
