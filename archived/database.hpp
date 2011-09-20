/*
 * database.hpp
 *
 *  Created on: 7 Mar 2011
 *      Author: dkorah
 */

#ifndef DATABASE_HPP_
#define DATABASE_HPP_

#include "operation_data.hpp"
#include "types.hpp"
#include "retention.hpp"

#include "../common/policy.hpp"
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

extern std::string hostname;

namespace database {

class retention : public basic_database< retention >
{
public:
	struct data
	{
		archiving::org_id_t org;
		archiving::user_id_t user;
		std::string uuid;
		applied_policy retention;
		types::file_format::file_format_t file_format;
		archiving::archivable::file_type::file_type_t file_type;
		uint64_t endpoint_id;

		struct compare
		{
			inline bool operator()(const data& lhs, const data& rhs) const
			{
				return lhs.org < rhs.org;
			}
		};

		typedef std::multiset<data, data::compare> multiset_t;
	};

	struct usage_data
	{
		archiving::org_id_t org;
		std::size_t real_size;
		std::size_t clear_size;
		time_t create_time;
		archiving::archivable::file_type::file_type_t filetype;

		struct compare
		{
			inline bool operator()(const usage_data& lhs, const usage_data& rhs) const
			{
				return lhs.org < rhs.org;
			}
		};

		typedef std::multiset<usage_data, usage_data::compare> multiset_t;
	};

	static inline const char* get_identifier()
	{
		static const char* identifier = "retention";
		return identifier;
	}

	retention(const boost::program_options::variables_map& confMap) :
		basic_database< retention >(confMap)
	{
	}

	std::pair< bool, app::retention > get_retention(const archiving::org_id_t& org, const std::string& identifier)
	{
		try {
			rml::MySQLConnection conn(*p_pool_); // scoped connection
			mysqlpp::Query query = conn->query();
			query << "SELECT"
					" OrgID,"
					" UserID,"
					" PolicyID,"
					" PolicyVersion, "
					" DAYOFMONTH(CreationDate), MONTH(CreationDate), YEAR(CreationDate),"
					" DAYOFMONTH(RetentionMinDate), MONTH(RetentionMinDate), YEAR(RetentionMinDate),"
					" DAYOFMONTH(RetentionMaxDate), MONTH(RetentionMaxDate), YEAR(RetentionMaxDate)"
					"FROM `ArchivingRetention`.`Retention_"<<org<<"` "
					"WHERE ReversedIdentifier=REVERSE(%0q:Identifier) AND OrgID=%1:OrgID"
					" AND Purged='n' AND RetentionMaxDate >= CURDATE()"
					"LIMIT 1";
			query.parse();
			mysqlpp::SQLQueryParms qp;
			qp << identifier << org;

			mysqlpp::UseQueryResult res = query.use(qp);
			if(mysqlpp::Row row = res.fetch_row()) {
				return std::make_pair(true, app::retention(
						archiving::org_id_t(rml::utils::strtoull(row[0].c_str())),    // OrgID
						archiving::user_id_t(rml::utils::strtoull(row[1].c_str())),   // UserID
						policy::id_type(rml::utils::strtoull(row[2].c_str())),      // PolicyID
						policy::version_type(rml::utils::strtoull(row[3].c_str())), // PolicyVersion
						applied_policy::ymd_t(                                      // CreationDate
								rml::utils::strtoull(row[4].c_str()),
								rml::utils::strtoull(row[5].c_str()),
								rml::utils::strtoull(row[6].c_str())),
						applied_policy::ymd_t(                                      // RetentionMinDate
								rml::utils::strtoull(row[7].c_str()),
								rml::utils::strtoull(row[8].c_str()),
								rml::utils::strtoull(row[9].c_str())),
						applied_policy::ymd_t(                                      // RetentionMinDate
								rml::utils::strtoull(row[10].c_str()),
								rml::utils::strtoull(row[11].c_str()),
								rml::utils::strtoull(row[12].c_str()))));
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

		return std::make_pair(false, app::retention());
	}

	bool save(const data::multiset_t& data, const usage_data::multiset_t& usage_data)
	{
		try {
			rml::MySQLConnection conn(*p_pool_);
			mysqlpp::Transaction transaction(*conn, false);

			// retention data
			{
				const data::multiset_t::const_iterator& end(data.end());

				data::multiset_t::const_iterator batch_itr = data.begin();
				const archiving::org_id_t& org(batch_itr->org);
				while(batch_itr != end) {
					std::pair< data::multiset_t::const_iterator, data::multiset_t::const_iterator > batch_range = data.equal_range(*batch_itr);

					mysqlpp::Query query = conn->query();
					query << "INSERT INTO `ArchivingRetention`.`Retention_"<<org<<"` \n"
							"( UUID, Format, OrgID, UserID, PolicyID, PolicyVersion, CreationDate, RetentionMinDate, RetentionMaxDate, MidnightOffsetSeconds, EndpointID, Type )"
							" VALUES ";

					for(data::multiset_t::const_iterator itr = batch_range.first; itr != batch_range.second; ++itr) {
						const applied_policy::ymd_t& creation_ymd(itr->retention.get_creation_ymd());
						const applied_policy::ymd_t& min_retention_ymd(itr->retention.get_min_retention_ymd());
						const applied_policy::ymd_t& max_retention_ymd(itr->retention.get_max_retention_ymd());
						const std::time_t& midnight_offset_seconds(itr->retention.get_midnight_offset_seconds());

						query << ((itr != batch_range.first)? ',': '\n')
								<<"( "
								<<"UNHEX(REPLACE( '"<<itr->uuid<<"', '-', '' )), "
								<<itr->file_format<<", "
								<<itr->org<<", "
								<<itr->user<<", "
								<<itr->retention.p_policy->id<<", "
								<<itr->retention.p_policy->version<<", "
								<<'\''<<applied_policy::ymd_t::year_type::value_type(creation_ymd.year)<<'-'<<applied_policy::ymd_t::month_type::value_type(creation_ymd.month)<<'-'<<applied_policy::ymd_t::day_type::value_type(creation_ymd.day)<<"', "
								<<'\''<<applied_policy::ymd_t::year_type::value_type(min_retention_ymd.year)<<'-'<<applied_policy::ymd_t::month_type::value_type(min_retention_ymd.month)<<'-'<<applied_policy::ymd_t::day_type::value_type(min_retention_ymd.day)<<"', "
								<<'\''<<applied_policy::ymd_t::year_type::value_type(max_retention_ymd.year)<<'-'<<applied_policy::ymd_t::month_type::value_type(max_retention_ymd.month)<<'-'<<applied_policy::ymd_t::day_type::value_type(max_retention_ymd.day)<<"', "
								<<midnight_offset_seconds<<", "
								<<itr->endpoint_id<<", "
								<<(int)(itr->file_type)
								<<" )\n";
					}
					std::cout << "SQL: "<<query.str()<<"\n" << std::endl;

					mysqlpp::SimpleResult res = query.execute();
					LogMessage("Retention data: "<<res.rows()<<" rows added for Org:"<<org);

					batch_itr = batch_range.second;
				}
			}


			// Usage Data
			{
				const usage_data::multiset_t::const_iterator& end(usage_data.end());

				usage_data::multiset_t::const_iterator batch_itr = usage_data.begin();
				while(batch_itr != end) {
					std::pair< usage_data::multiset_t::const_iterator, usage_data::multiset_t::const_iterator > batch_range = usage_data.equal_range(*batch_itr);

					const archiving::org_id_t& org(batch_itr->org);

					mysqlpp::Query query = conn->query();
					query << "INSERT INTO `ArchivingRetention`.`Usage_"<<org<<"` \n"
							"( `Hour`, `Type`, `OrgID`, `Count`, `ClearUsage`, `RealUsage` )"
							" VALUES ";

					for(usage_data::multiset_t::const_iterator itr = batch_range.first; itr != batch_range.second; ++itr) {
						char hour_res_date[255];
						{
							tm tm_;
							gmtime_r(&itr->create_time, &tm_);
							strftime(hour_res_date, 255, "%Y-%m-%d %H:00:00", &tm_);
						}

						query << (itr != batch_range.first? ',': '\n')
								<<"( "
								<<'\''<<hour_res_date<<"', "
								<<'\''<<itr->filetype<<"', "
								<<itr->org<<", "
								<<"1, "
								<<itr->clear_size<<", "
								<<itr->real_size
								<<" )\n";
					}
					query << " ON DUPLICATE KEY UPDATE `Count`=`Count`+VALUES(`Count`), `ClearUsage`=`ClearUsage`+VALUES(`ClearUsage`), `RealUsage`=`RealUsage`+VALUES(`RealUsage`)"
							"\n";

					std::cout << "SQL: "<<query.str()<<"\n" << std::endl;

					mysqlpp::SimpleResult res = query.execute();
					LogMessage("Usage data added for Org:"<<org);

					batch_itr = batch_range.second;
				}
			}

			transaction.commit();

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

	/*
	void save(const archiving::org_id& org, const archiving::user_id& user, const std::string& identifier, const applied_policy& retention)
	{
		rml::MySQLConnection conn(*p_pool_); // scoped connection
		mysqlpp::Query query = conn->query();
		query << "INSERT INTO `ArchivingRetention`.`Retention_"<<org<<"` "
				"SET Identifier=%0q:Identifier, ReversedIdentifier=REVERSE(%0q:Identifier)"
				" OrgID=%1:OrgID,"
				" UserID=%2:UserID,"
				" CreationDate='%3:CreationDateYear-%4:CreationDateMonth-%5:CreationDateDay',"
				" RetentionMinDate='%6:RetentionMinDateYear-%7:RetentionMinDateMonth-%8:RetentionMinDateDay',"
				" RetentionMaxDate='%9:RetentionMaxDateYear-%10:RetentionMaxDateMonth-%11:RetentionMaxDateDay',"
				" PolicyID=%12:PolicyID,"
				" PolicyVersion=%13:PolicyVersion"
				;
		query.parse();

		const applied_policy::ymd_t& creation_ymd(retention.get_creation_ymd());
		const applied_policy::ymd_t& min_retention_ymd(retention.get_min_retention_ymd());
		const applied_policy::ymd_t& max_retention_ymd(retention.get_max_retention_ymd());

		mysqlpp::SQLQueryParms qp; qp

				<< identifier
				<< org
				<< user

				<< applied_policy::ymd_t::year_type::value_type(creation_ymd.year)
				<< applied_policy::ymd_t::month_type::value_type(creation_ymd.month)
				<< applied_policy::ymd_t::day_type::value_type(creation_ymd.day)

				<< applied_policy::ymd_t::year_type::value_type(min_retention_ymd.year)
				<< applied_policy::ymd_t::month_type::value_type(min_retention_ymd.month)
				<< applied_policy::ymd_t::day_type::value_type(min_retention_ymd.day)

				<< applied_policy::ymd_t::year_type::value_type(max_retention_ymd.year)
				<< applied_policy::ymd_t::month_type::value_type(max_retention_ymd.month)
				<< applied_policy::ymd_t::day_type::value_type(max_retention_ymd.day)

				<< retention.p_policy->id
				<< retention.p_policy->version;

		LogVerbose("SQL: "<<query.str(qp));
		query.execute(qp);
	}
	*/
};

class internal : public basic_database< internal >
{
public:
	static inline const char* get_identifier()
	{
		static const char* identifier = "internal";
		return identifier;
	}

	internal(const boost::program_options::variables_map& confMap) :
		basic_database< internal >(confMap)
	{
	}

	bool save(operation_data::pointer& opdata)
	{
		try {
			rml::MySQLConnection conn(*p_pool_); // scoped connection
			mysqlpp::Transaction transaction(*conn, false);

			const operation_data::detail& opdetail(*(opdata->p_detail));

			if(!opdetail.database_id) {
				LogVerbose("Insert: "<<opdetail.identifier);

				mysqlpp::Query query = conn->query();
				query << "INSERT INTO `ArchivingInternal`.`ArchiveDQueue`"

						" SET HostName=%0q:HostName,"

						" FileType=%1:FileType,"
						" OrgID=%2:OrgID,"
						" UUID=UNHEX(REPLACE(%3q:UUID, '-', '')),"

						" MetaData=%4q:MetaData,"

						" PolicyID=%5:PolicyID,"
						" PolicyVersion=%6:PolicyVersion,"

						" TargetFileFormat=%7:TargetFileFormat,"
						" EndPointID=%8:EndPointID,"

						" CurrentFileFormat=%9:CurrentFileFormat,"
						" RetryCount=%10:RetryCount,"
						" OpState=%11:OpState,"

						" CreateTimeEpoch=%12:CreateTimeEpoch"
						;
				query.parse();

				mysqlpp::SQLQueryParms qp;
				qp 		<< app::hostname

						<< opdetail.identifier.filetype
						<< opdetail.identifier.org
						<< opdetail.identifier.uuid

						<< opdetail.p_metadata->to_string()

						<< (opdetail.policy.p_policy? opdetail.policy.p_policy->id: -1)
						<< (opdetail.policy.p_policy? opdetail.policy.p_policy->version: 0)

						<< ( opdetail.p_endpoint? opdetail.target_file_format: 0 ) // bodge! no target file format if there is no endpoint!
						<< ( opdetail.p_endpoint? opdetail.p_endpoint->id: 0 )

						<< opdetail.current_file_format
						<< opdetail.retry
						<< opdetail.state

						<< mysqlpp::sql_int(opdetail.policy.get_creation_epoch())
						;

				LogVerbose("SQL: "<<query.str(qp));
				query.execute(qp);

				opdata->p_detail->database_id = query.insert_id();
			}
			else {
				LogVerbose("Update "<<opdetail.identifier);

				mysqlpp::Query query = conn->query();
				query << "UPDATE `ArchivingInternal`.`ArchiveDQueue`"
						" SET CurrentFileFormat=%0:CurrentFileFormat,"
						" RetryCount=%1:RetryCount,"
						" OpState=%2:OpState"
						" WHERE ID=%3:ID "
						"  AND FileType=%4:FileType AND OrgID=%5:OrgID AND UUID=UNHEX(REPLACE(%6q:UUID, '-', ''))"
						;
				query.parse();

				mysqlpp::SQLQueryParms qp;
				qp << opdetail.current_file_format
						<< opdetail.retry
						<< opdetail.state
						<< opdetail.database_id
						<< opdetail.identifier.filetype
						<< opdetail.identifier.org
						<< opdetail.identifier.uuid
						;

				LogVerbose("SQL: "<<query.str(qp));
				query.execute(qp);
			}

			// save files list
			if(!opdata->p_detail->files_map.empty()) {
				mysqlpp::SQLQueryParms qp;
				mysqlpp::Query query = conn->query();
				query << "INSERT INTO `ArchivingInternal`.`ArchiveDQueueFiles`"
						" (ArchiveDQueueID, FileFormat, Path) VALUES"
						;

				const files_map_t::iterator& end = opdata->p_detail->files_map.end();
				files_map_t::iterator itr = opdata->p_detail->files_map.begin();
				bool first = true;
				for(unsigned int c = 0; itr != end; ++itr, c += 3) {
					query << (!first? ',': ' ') << "( %"<<(c+0)<<":ArchiveDQueueID, %"<<(c+1)<<":FileFormat, %"<<(c+2)<<"q:Path )";
					qp << opdetail.database_id << itr->first << itr->second->get_filename();
					first = false;
				}

				query << " ON DUPLICATE KEY UPDATE Path=VALUES(Path)";

				query.parse();
				LogVerbose("SQL: "<<query.str(qp));

				query.execute(qp);
			}

			transaction.commit();

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

	std::string clear_cache_get_path(const archiving::org_id_t org_id, const std::string& uuid, const types::file_format::file_format_t& file_format)
	{
		rml::MySQLConnection conn(*p_pool_); // scoped connection
		std::string path;
		try {
			mysqlpp::Transaction trans(*conn);

			uint64_t id;

			{
				mysqlpp::Query query = conn->query();
				query << "SELECT ID, Path FROM `ArchivingInternal`.`ArchiveDClearCache` "
						"WHERE OrgID=%0:OrgID AND UUID=UNHEX(REPLACE(%1q:Identifier, '-', '')) AND FileFormat=%2:FileFormat "
						"FOR UPDATE";
				query.parse();

				mysqlpp::SQLQueryParms qp;
				qp << org_id << uuid << file_format;

				LogVerbose("SQL: "<<query.str(qp));

				mysqlpp::UseQueryResult res = query.use(qp);

				if(mysqlpp::Row row = res.fetch_row()) {
					path = row[1].c_str();
					id = rml::utils::strtoull(row[0].c_str());
				}
			}

			if(!path.empty() && id) {
				mysqlpp::Query query = conn->query();
				query << "UPDATE ArchivingInternal.ArchiveDClearCache SET AccessTime=NOW() WHERE ID=" << id;
				LogMessage("SQL: "<<query.str());
				query.execute();

				return path;
			}

			throw rml::exception("No path found in clear cache");
		}
		catch(const mysqlpp::BadQuery& e) {
			if(e.errnum() == ER_LOCK_DEADLOCK || e.errnum() == ER_LOCK_WAIT_TIMEOUT) {
				LogWarning("Ignoring UPDATE failure: "<<e.what());
				return path;
			}

			LogError("Bad Query: "<<e.what());
			throw;
		}
		catch(const mysqlpp::Exception& e) {
			LogError("Exception: "<<e.what());
			throw;
		}
	}

	bool clear_cache_put_path(
			const archiving::org_id_t org_id,
			const std::string& uuid,
			const types::file_format::file_format_t& file_format,
			const std::string& path,
			const bool is_hardlink=true)
	{
		try {
			rml::MySQLConnection conn(*p_pool_);

			mysqlpp::Query query = conn->query();
			query << "INSERT INTO `ArchivingInternal`.`ArchiveDClearCache` "
					"SET OrgID=%0:OrgID,"
					" UUID=UNHEX(REPLACE(%1q:Identifier, '-', '')),"
					" FileFormat=%2:FileFormat,"
					" Path=%3q:Path,"
					" HardLink=%4q:HardLink,"
					" CreateTime=NOW(),"
					" AccessTime=NOW() "
					"ON DUPLICATE KEY UPDATE"
					" Path=%3q:Path,"
					" HardLink=%4q:HardLink,"
					" CreateTime=NOW(),"
					" AccessTime=NOW()"
					;
			query.parse();

			mysqlpp::SQLQueryParms qp;
			qp << org_id << uuid << file_format << path << (is_hardlink? "yes": "no");
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

#endif /* DATABASE_HPP_ */
