/*
 * database.hpp
 *
 *  Created on: 7 Mar 2011
 *      Author: dkorah
 */

#ifndef CDRMUNCH_DATABASE_HPP_
#define CDRMUNCH_DATABASE_HPP_

#include "whisper_client.hpp"

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

class cdrmunch : public basic_database< cdrmunch >
{
public:
	static inline const char* get_identifier()
	{
		static const char* identifier = "cdrmunch";
		return identifier;
	}

	cdrmunch(const boost::program_options::variables_map& confMap) :
		basic_database< cdrmunch >(confMap)
	{
	}

	bool save(whisper::pointer p_whisper)
	{
		try {
			rml::MySQLConnection conn(*p_pool_); // scoped connection

			mysqlpp::Query query = conn->query();
			query << "INSERT IGNORE INTO cdr_queue (path, create_time, next_attempt, retry, fs_host, uuid)"
					 " VALUES "
					 "( %0q:path, FROM_UNIXTIME( %1:create_time ), NOW(), 0, %2q:fs_host, UNHEX( REPLACE( %3q:uuid, '-', '' ) ) )";
			query.parse();

			mysqlpp::SQLQueryParms qp;
			qp << p_whisper->cdr_path << p_whisper->create_time << p_whisper->fs_host << p_whisper->uuid;

			SQL_DEBUG(query.str(qp));
			query.execute(qp);

			return true;
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
