//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
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
#include <rml/database/mysql/transactional_connection.hpp>
#include <rml/database/mysql/errors.hpp>
#include <rml/exception.h>
#include <rml/logger.h>

#include <boost/program_options.hpp>
#include <boost/thread/thread.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <set>

#include <time.h>

namespace app {

namespace database {

namespace rmldb = rml::database::mysql;

class cdrmunch : public rmldb::basic_database<cdrmunch, rmldb::transactional_connection>
{
public:
	static inline const char* get_identifier()
	{
		return "cdrmunch";
	}

	explicit cdrmunch(const boost::program_options::variables_map& confMap) :
			rmldb::basic_database<cdrmunch, rmldb::transactional_connection>(confMap)
	{}

	bool save(const whisper::pointer& p_whisper)
	{
		try {
			boost::shared_ptr<rmldb::transactional_connection> conn = get_connection_wait_for(boost::chrono::seconds(5));
			mysqlpp::Query query = conn->query();
			query << "INSERT IGNORE INTO cdr_queue (path, create_time, next_attempt, retry, fs_host, uuid)"
					 " VALUES "
					 "( %0q:path, FROM_UNIXTIME( %1:create_time ), NOW(), 0, %2q:fs_host, UNHEX( REPLACE( %3q:uuid, '-', '' ) ) )";
			query.parse();

			mysqlpp::SQLQueryParms qp;
			qp << p_whisper->cdr_path << p_whisper->create_time << p_whisper->fs_host << p_whisper->uuid;

			MYSQL_QUERY_EXECUTE(DEFAULT, conn, query, qp);

			return true;
		}
		MYSQL_CATCH_LOG_ERROR(e)

		return false;
	}
};

}  // namespace database

}  // namespace app

#endif /* CDRMUNCH_DATABASE_HPP_ */
