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
		std::string leg_uuid;
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
		uint64_t endpoint_id;

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

	/// TODO NEED TO MAINTAIN THIS!!!
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

			{
				mysqlpp::Query query = conn->query();
				query << "SET time_zone = '+00:00'";
				query.execute();
			}

			// retention data
			{
				const data::multiset_t::const_iterator& end(data.end());

				data::multiset_t::const_iterator batch_itr = data.begin();
				const archiving::org_id_t& org(batch_itr->org);
				while(batch_itr != end) {
					std::pair< data::multiset_t::const_iterator, data::multiset_t::const_iterator > batch_range = data.equal_range(*batch_itr);

					mysqlpp::Query query = conn->query();
					query << "INSERT INTO `ArchivingRetention`.`Retention_"<<org<<"` \n"
							"( UUID, LegUUID, Format, OrgID, UserID, PolicyID, PolicyVersion, CreationDate, RetentionMinDate, RetentionMaxDate, MidnightOffsetSeconds, EndpointID, Type )"
							" VALUES ";

					for(data::multiset_t::const_iterator itr = batch_range.first; itr != batch_range.second; ++itr) {
						const applied_policy::ymd_t& creation_ymd(itr->retention.get_creation_ymd());
						const applied_policy::ymd_t& min_retention_ymd(itr->retention.get_min_retention_ymd());
						const applied_policy::ymd_t& max_retention_ymd(itr->retention.get_max_retention_ymd());
						const std::time_t& midnight_offset_seconds(itr->retention.get_midnight_offset_seconds());

						query << ((itr != batch_range.first)? ',': '\n')
								<<"( "
								<<"UNHEX(REPLACE( '"<<itr->uuid<<"', '-', '' )), ";

						if(!itr->leg_uuid.empty())
							query << "UNHEX(REPLACE( '"<<itr->leg_uuid<<"', '-', '' )), ";
						else
							query << "NULL, ";

						query << itr->file_format<<", "
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
					query << "ON DUPLICATE KEY UPDATE EndpointID=VALUES(EndpointID)";

					std::cout << "SQL: "<<query.str()<< std::endl;

					mysqlpp::SimpleResult res = query.execute();
					LogVerbose("Retention data: "<<res.rows()<<" rows added for Org:"<<org);

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
							"( `Hour`, `Type`, `OrgID`, `Count`, `ClearUsage`, `RealUsage`, `EndpointID` )"
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
								<<itr->real_size<<", "
								<<itr->endpoint_id
								<<" )\n";
					}
					query << " ON DUPLICATE KEY UPDATE `Count`=`Count`+VALUES(`Count`), `ClearUsage`=`ClearUsage`+VALUES(`ClearUsage`), `RealUsage`=`RealUsage`+VALUES(`RealUsage`)"
							"\n";

					std::cout << "SQL: "<<query.str()<<"\n" << std::endl;

					mysqlpp::SimpleResult res = query.execute();
					LogVerbose("Usage data added for Org:"<<org);

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
				LogDebug("Insert: "<<opdetail.identifier);

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

						" CallCreateTimeEpoch=%12:CallCreateTimeEpoch,"
						" ArrivalTime=FROM_UNIXTIME(%13:ArrivalTime),"

						" ClearSize=%14:ClearSize,"
						" RealSize=%15:RealSize"
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
						<< mysqlpp::sql_int(opdetail.arrival_time)

						<< opdetail.clear_size
						<< opdetail.real_size
						;

				std::cout << "SQL: "<<query.str(qp) << std::endl;
				query.execute(qp);

				opdata->p_detail->database_id = query.insert_id();
			}
			else {
				LogDebug("Update "<<opdetail.identifier);

				mysqlpp::Query query = conn->query();
				query << "UPDATE `ArchivingInternal`.`ArchiveDQueue` "
						"SET"
						" CurrentFileFormat=%0:CurrentFileFormat,"
						" RetryCount=%1:RetryCount,"
						" OpState=%2:OpState "
						" ClearSize=%3:ClearSize,"
						" RealSize=%4:RealSize "
						"WHERE"
						" ID=%5:ID"
						" AND FileType=%6:FileType AND OrgID=%7:OrgID AND UUID=UNHEX(REPLACE(%8q:UUID, '-', ''))"
						;
				query.parse();

				mysqlpp::SQLQueryParms qp;
				qp << opdetail.current_file_format
						<< opdetail.retry
						<< opdetail.state
						<< opdetail.clear_size
						<< opdetail.real_size
						<< opdetail.database_id
						<< opdetail.identifier.filetype
						<< opdetail.identifier.org
						<< opdetail.identifier.uuid
						;

				std::cout << "SQL: "<<query.str(qp) << std::endl;
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
				std::cout << "SQL: "<<query.str(qp) << std::endl;

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

	void find_retriables(
			const std::string& arcd_hostname,
			policy::cached_finder_t fn_cached_policy_finder,
			const boost::function<bool(const operation_data::pointer&)>& fn_is_already_loaded,
			const storage::cached_endpoint_finder_t& fn_cached_endpoint_finder,
			operation_data::channel_t & channel,
			bool& have_more_to_load)
	{
		static bool first = true;
		static uint64_t first_max_id = 0;
		static uint64_t max_id = 0;

		try {
			rml::MySQLConnection conn(*p_pool_); // scoped connection

			if(first) {
				mysqlpp::Query query = conn->query();
				query << "SELECT MAX(ID) FROM ArchivingInternal.ArchiveDQueue"
						" WHERE HostName=%0q:HostName AND RetryCount<3 AND OpState!=%1:OpStateAllDone";
				query.parse();

				mysqlpp::SQLQueryParms qp;
				qp << arcd_hostname << operation_state::all_done;

				std::cout << "SQL: "<<query.str(qp) << std::endl;

				mysqlpp::UseQueryResult res = query.use(qp);
				while(mysqlpp::Row row = res.fetch_row())
					if(row[0] != "NULL")
						first_max_id = rml::utils::strtoull(row[0].c_str(), 0ULL);

				first = false;
			}

			if(first_max_id && max_id <= first_max_id) {
				mysqlpp::Query query = conn->query();
				query << "SELECT"
						" ID, HostName, MetaData, PolicyID, PolicyVersion, FileType, OrgID,"
						" LCASE(CONCAT(HEX(SUBSTR(UUID, 1, 4)),'-',HEX(SUBSTR(UUID, 5, 2)),'-',HEX(SUBSTR(UUID, 7, 2)),'-',HEX(SUBSTR(UUID, 9, 2)),'-',HEX(SUBSTR(UUID, 11)))) UUID,"
						" TargetFileFormat, EndPointID, CurrentFileFormat, RetryCount, OpState, CallCreateTimeEpoch, ArrivalTime, ClearSize, RealSize "
						"FROM ArchivingInternal.ArchiveDQueue "
						"WHERE HostName=%0q:HostName AND RetryCount<3 AND OpState!=%1:OpStateAllDone "
						" AND c.ID <= %1:FirstMaxID AND c.ID > %2:MaxID "
						"LIMIT 100";
				query.parse();

				mysqlpp::SQLQueryParms qp;
				qp << arcd_hostname << operation_state::all_done << first_max_id << max_id;

				std::cout << "SQL: "<<query.str(qp) << std::endl;

				mysqlpp::UseQueryResult res = query.use(qp);
				while(mysqlpp::Row row = res.fetch_row()) {
					max_id = rml::utils::strtoll(row[0].c_str());
					const archiving::org_id_t org(rml::utils::strtoull(row[6].c_str(), 0ULL));

					try {
						const uint64_t& item_id(rml::utils::strtoull(row[0].c_str(), 0ULL));

						// TODO Ignoring PolicyVersion here; we need to revisit this
						const policy::pointer& p_policy(
								fn_cached_policy_finder(std::make_pair< archiving::org_id_t, policy::id_type >(
										org, policy::id_type(rml::utils::strtoull(row[3].c_str(), 0ULL)))));

						const storage::basic_endpoint::pointer& p_endpoint(
								fn_cached_endpoint_finder(std::make_pair< archiving::org_id_t, archiving::endpoint_id_t >(
										org, archiving::endpoint_id_t(rml::utils::strtoull(row[9].c_str(), 0ULL)))));

						files_map_t files_map;
						{
							rml::MySQLConnection conn(*p_pool_);
							mysqlpp::Query query = conn->query();
							query << "SELECT FileFormat, Path FROM ArchivingInternal.ArchiveDQueueFiles WHERE ArchiveDQueueID=%0:ID";
							query.parse();

							mysqlpp::SQLQueryParms qp;
							qp << item_id;
							std::cout << "SQL: "<<query.str(qp) << std::endl;

							mysqlpp::UseQueryResult res = query.use(qp);
							while(mysqlpp::Row row = res.fetch_row()) {
								const std::pair<files_map_t::iterator, bool>& ret(files_map.insert(std::make_pair< types::file_format::file_format_t, rml::utils::tmp_fstream::pointer >(
										types::file_format::file_format_t(rml::utils::strtoull(row[0].c_str(), 0ULL)),
										rml::utils::tmp_fstream::pointer(
												new rml::utils::tmp_fstream(
														row[1].c_str(),
														std::ios_base::openmode(std::fstream::in | std::fstream::out | std::fstream::binary))))));
								if(ret.second)
									ret.first->second->close();
							}
						}

						operation_data::pointer p_opdata(new operation_data());
						p_opdata->p_detail = operation_data::detail::pointer(new operation_data::detail(
								rml::utils::strtoull(row[14].c_str(), 0ULL),
								archiving::archivable::file_type::file_type_t(rml::utils::strtoull(row[5].c_str(), 0ULL)),
								archiving::org_id_t(rml::utils::strtoull(row[6].c_str(), 0ULL)),
								row[7].c_str(),
								row[2].c_str(),
								p_policy, rml::utils::strtoull(row[13].c_str(), 0ULL),
								p_endpoint,
								types::file_format::file_format_t(rml::utils::strtoull(row[8].c_str(), 0ULL)),
								rml::utils::strtoull(row[15].c_str(), 0ULL), rml::utils::strtoull(row[16].c_str(), 0ULL),
								types::file_format::file_format_t(rml::utils::strtoull(row[10].c_str(), 0ULL)),
								files_map,
								operation_state::operation_state_t(rml::utils::strtoull(row[12].c_str(), 0ULL)),
								item_id,
								rml::utils::strtoull(row[11].c_str(), 0ULL)));

						channel.produce(p_opdata);
					}
					catch(const std::exception& e) {
						LogWarning("Exception: "<<e.what());
					}
				}
			}

			have_more_to_load = (first_max_id && (max_id < first_max_id)); // more to load?
		}
		catch(const mysqlpp::Exception& e) {
			LogWarning("MySQL Error: "<<e.what());
		}
		catch(const std::exception& e) {
			LogWarning("Exception: "<<e.what());
		}
	}

	bool purge(const std::set< operation_data::pointer >& purgeables)
	{
		try {
			rml::MySQLConnection conn(*p_pool_);
			mysqlpp::Transaction trans(*conn);

			mysqlpp::Query query = conn->query();

			query << "DELETE FROM ArchivingInternal.ArchiveDQueue WHERE ID IN (";

			for(std::set< operation_data::pointer >::const_iterator itr = purgeables.begin(); itr != purgeables.end(); ++itr) {
				if(itr != purgeables.begin())
					query << ',';
				query << '\n'<<(*itr)->p_detail->database_id << " /*"<<(*itr)->p_detail->identifier<<"*/";
			}

			query << ')';

			std::cout << "SQL: "<<query.str() << std::endl;

			query.execute();
			trans.commit();

			return true;
		}
		catch(const mysqlpp::BadQuery& e) {
			LogError("Bad MySQL Query: "<<e.what());
		}
		catch(const mysqlpp::Exception& e) {
			LogError("MySQL Exception: "<<e.what());
		}
		catch(const std::exception& e) {
			LogError("Exception: "<<e.what());
		}

		return false;
	}

	std::string clear_cache_get_path(const archiving::org_id_t org_id, const std::string& uuid, const types::file_format::file_format_t& file_format)
	{
		std::string path;
		try {
			rml::MySQLConnection conn(*p_pool_); // scoped connection
			mysqlpp::Transaction trans(*conn);

			uint64_t id;

			{
				mysqlpp::Query query = conn->query();
				query << "SELECT ID, Path FROM `ArchivingInternal`.`ArchiveDClearCache` "
						"WHERE OrgID=%0:OrgID AND UUID=UNHEX(REPLACE(%1q:Identifier, '-', '')) AND FileFormat=%2:FileFormat AND HostName=%3q:HostName "
						"FOR UPDATE";
				query.parse();

				mysqlpp::SQLQueryParms qp;
				qp << org_id << uuid << file_format << app::hostname;

				std::cout << "SQL: "<<query.str(qp) << std::endl;

				mysqlpp::UseQueryResult res = query.use(qp);

				if(mysqlpp::Row row = res.fetch_row()) {
					path = row[1].c_str();
					id = rml::utils::strtoull(row[0].c_str());
				}
			}

			if(!path.empty() && id) {
				mysqlpp::Query query = conn->query();
				query << "UPDATE ArchivingInternal.ArchiveDClearCache SET AccessTime=NOW() WHERE ID=" << id;
				std::cout << "SQL: "<<query.str() << std::endl;
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
					" AccessTime=NOW(),"
					" HostName=%5q:HostName "
					"ON DUPLICATE KEY UPDATE"
					" Path=%3q:Path,"
					" HardLink=%4q:HardLink,"
					" CreateTime=NOW(),"
					" AccessTime=NOW(),"
					" HostName=%5q:HostName"
					;
			query.parse();

			mysqlpp::SQLQueryParms qp;
			qp << org_id << uuid << file_format << path << (is_hardlink? "yes": "no") << app::hostname;

			std::cout << "SQL: "<<query.str(qp)<< std::endl;
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
