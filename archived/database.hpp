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
#include "policy.hpp"
#include "retention.hpp"

#include <rml/DBConnectionPool.h>
#include <rml/exception.h>
#include <rml/logger.h>

#include <boost/program_options.hpp>

#include <set>

namespace app {

namespace database {

template < typename database_type >
class basic_database : public boost::noncopyable
{
protected:
	const boost::program_options::variables_map& confMap_;
	rml::DBConnectionPool pool_;

public:
	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		rml::DBConnectionPool::describe_config(optsDesc, database_type::get_identifier());
	}

	basic_database(const boost::program_options::variables_map& confMap) :
		confMap_(confMap), pool_(confMap_, database_type::get_identifier())
	{
	}

	virtual ~basic_database()
	{
		LogMessage("database['"<<database_type::get_identifier() << "] destroyed");
	}
};

class retention : public basic_database< retention >
{
public:
	struct data
	{
		archiving::org_id org;
		archiving::user_id user;
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
	};

	typedef std::multiset<data, data::compare> data_multiset;

	static inline const char* get_identifier()
	{
		static const char* identifier = "retention";
		return identifier;
	}

	retention(const boost::program_options::variables_map& confMap) :
		basic_database< retention >(confMap)
	{
	}

	std::pair< bool, app::retention > get_retention(const archiving::org_id& org, const std::string& identifier)
	{
		rml::MySQLConnection conn(pool_); // scoped connection
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
					archiving::org_id(rml::utils::strtoull(row[0].c_str())),    // OrgID
					archiving::user_id(rml::utils::strtoull(row[1].c_str())),   // UserID
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

		return std::make_pair(false, app::retention());
	}

	void save(data_multiset& data)
	{
		while(data.begin() != data.end()) {
			data_multiset::iterator b_itr = data.begin();
			std::pair< data_multiset::iterator, data_multiset::iterator > range = data.equal_range(*b_itr);

			const archiving::org_id& org(b_itr->org);

			rml::MySQLConnection conn(pool_); // scoped connection
			mysqlpp::SQLQueryParms qp;
			mysqlpp::Query query = conn->query();
			query << "INSERT INTO `ArchivingRetention`.`Retention_"<<org<<"` "
					"("
						" UUID, Format, OrgID, UserID, PolicyID, PolicyVersion, CreationDate, RetentionMinDate, RetentionMaxDate, EndpointID, Type "
					")"
					" VALUES"
					;
			int values_count = 0;
			for(data_multiset::iterator itr = range.first; itr != range.second; ++itr) {
				query << (values_count? ',': ' ') << "( UNHEX(REPLACE(%"<<(values_count*17+0)<<"q:UUID_"<<values_count<<", '-', '')), %"<<(values_count*17+1)<<":Format_"<<values_count<<", "
				"%"<<(values_count*17+2)<<":OrgID_"<<values_count<<", %"<<(values_count*17+3)<<":UserID_"<<values_count<<", "
				"%"<<(values_count*17+4)<<":PolicyID_"<<values_count<<", %"<<(values_count*17+5)<<":PolicyVersion_"<<values_count<<", "
				"'%"<<(values_count*17+6)<<":CreationDateYear_"<<values_count<<"-%"<<(values_count*17+7)<<":CreationDateMonth_"<<values_count<<"-%"<<(values_count*17+8)<<":CreationDateDay_"<<values_count<<"', "
				"'%"<<(values_count*17+9)<<":RetentionMinDateYear_"<<values_count<<"-%"<<(values_count*17+10)<<":RetentionMinDateMonth_"<<values_count<<"-%"<<(values_count*17+11)<<":RetentionMinDateDay_"<<values_count<<"', "
				"'%"<<(values_count*17+12)<<":RetentionMaxDateYear_"<<values_count<<"-%"<<(values_count*17+13)<<":RetentionMaxDateMonth_"<<values_count<<"-%"<<(values_count*17+14)<<":RetentionMaxDateDay_"<<values_count<<"', "
				"%"<<(values_count*17+15)<<":EndpointID_"<<values_count<<", %"<<(values_count*17+16)<<":Type_"<<values_count<<" )";

				const applied_policy::ymd_t& creation_ymd(itr->retention.get_creation_ymd());
				const applied_policy::ymd_t& min_retention_ymd(itr->retention.get_min_retention_ymd());
				const applied_policy::ymd_t& max_retention_ymd(itr->retention.get_max_retention_ymd());

				qp << itr->uuid << itr->file_format

						<< itr->org << itr->user
						<< itr->retention.p_policy->id << itr->retention.p_policy->version

						<< applied_policy::ymd_t::year_type::value_type(creation_ymd.year)
						<< applied_policy::ymd_t::month_type::value_type(creation_ymd.month)
						<< applied_policy::ymd_t::day_type::value_type(creation_ymd.day)

						<< applied_policy::ymd_t::year_type::value_type(min_retention_ymd.year)
						<< applied_policy::ymd_t::month_type::value_type(min_retention_ymd.month)
						<< applied_policy::ymd_t::day_type::value_type(min_retention_ymd.day)

						<< applied_policy::ymd_t::year_type::value_type(max_retention_ymd.year)
						<< applied_policy::ymd_t::month_type::value_type(max_retention_ymd.month)
						<< applied_policy::ymd_t::day_type::value_type(max_retention_ymd.day)

						<< itr->endpoint_id << itr->file_type;

				values_count++;
			}

			LogVerbose("SQL (not parsed): "<<query.str());

			query.parse();
			LogVerbose("SQL: "<<query.str(qp));
			mysqlpp::SimpleResult res = query.execute(qp);
			LogMessage("Retention data: "<<res.rows()<<" rows added for Org:"<<org);

			data.erase(range.first, range.second);
		}
	}

	void save(const archiving::org_id& org, const archiving::user_id& user, const std::string& identifier, const applied_policy& retention)
	{
		// TODO batch these queries! (with the danger of data loss in the event of a crash; but we still have this info in meta data)

		rml::MySQLConnection conn(pool_); // scoped connection
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

	void save(operation_data::pointer& opdata)
	{
		rml::MySQLConnection conn(pool_); // scoped connection

		const operation_data::detail& opdetail(*(opdata->p_detail));

		if(!opdata->database_id) {
			LogVerbose("Insert: "<<opdetail.identifier);

			mysqlpp::Query query = conn->query();
			query << "INSERT INTO `ArchivingInternal`.`ArchiveDProcessing`"
					" SET ReversedIdentifier=REVERSE(%0q:Identifier),"
					" MetaData=%1q:MetaData,"
					" FileType=%2:FileType,"
					" OrgID=%3:OrgID,"
					" PolicyID=%4:PolicyID,"
					" PolicyVersion=%5:PolicyVersion,"
					" TargetFileFormat=%6:TargetFileFormat,"
					" EndPointID=%7:EndPointID,"
					" CurrentFileFormat=%8:CurrentFileFormat,"
					" RetryCount=%9:RetryCount,"
					" OpState=%10:OpState,"
					" CreateTimeEpoch=%11:CreateTimeEpoch"
					;
			query.parse();

			mysqlpp::SQLQueryParms qp;
			qp << opdetail.identifier
					<< opdetail.p_metadata->to_string()
					<< opdetail.file_type
					<< opdetail.org_id
					<< opdetail.policy.p_policy->id
					<< opdetail.policy.p_policy->version
					<< opdetail.target_file_format
					<< opdetail.p_endpoint->identifier
					<< opdetail.current_file_format
					<< opdetail.retry
					<< opdata->state
					<< mysqlpp::sql_int(opdetail.policy.get_creation_epoch())
					;

			LogVerbose("SQL: "<<query.str(qp));
			query.execute(qp);

			opdata->database_id = query.insert_id();
		}
		else {
			LogVerbose("Update "<<opdetail.identifier);

			mysqlpp::Query query = conn->query();
			query << "UPDATE `ArchivingInternal`.`ArchiveDProcessing`"
					" SET CurrentFileFormat=%0:CurrentFileFormat,"
					" RetryCount=%1:RetryCount,"
					" OpState=%2:OpState,"
					" WHERE ID=%3:ID AND ReversedIdentifier=REVERSE(%4q:Identifier)"
					;
			query.parse();

			mysqlpp::SQLQueryParms qp;
			qp << opdetail.current_file_format
					<< opdetail.retry
					<< opdata->state
					<< opdata->database_id
					<< opdetail.identifier;

			LogVerbose("SQL: "<<query.str(qp));
			query.execute(qp);
		}
	}

	std::string clear_cache_get_path(const archiving::org_id org_id, const std::string& identifier, const types::file_format::file_format_t& file_format)
	{
		rml::MySQLConnection conn(pool_); // scoped connection
		try {
			mysqlpp::Transaction trans(*conn);

			std::string path;
			uint64_t id;

			{
				mysqlpp::Query query = conn->query();
				query << "SELECT ID, Path FROM `ArchivingInternal`.`ArchiveDClearCache` "
						"WHERE OrgID=%0:OrgID AND ReversedIdentifier=REVERSE(%1q:Identifier) AND FileFormat=%2:FileFormat "
						"FOR UPDATE";
				query.parse();

				mysqlpp::SQLQueryParms qp;
				qp << org_id << identifier << file_format;

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

			LogMessage("No path found in clear cache");

			throw rml::exception("No path found in clear cache");
		}
		catch(const mysqlpp::BadQuery& e) {
			LogCritical("Bad Query: "<<e.what());
			throw;
		}
		catch(const mysqlpp::Exception& e) {
			LogError("Exception: "<<e.what());
			throw;
		}
	}

	void clear_cache_put_path(
			const archiving::org_id org_id,
			const std::string& identifier,
			const types::file_format::file_format_t& file_format,
			const std::string& path,
			const bool is_hardlink=true)
	{
		rml::MySQLConnection conn(pool_);
		try {
			mysqlpp::Query query = conn->query();
			query << "INSERT INTO `ArchivingInternal`.`ArchiveDClearCache` "
					"SET OrgID=%0:OrgID,"
					" ReversedIdentifier=REVERSE(%1q:Identifier),"
					" FileFormat=%2:FileFormat,"
					" Path=%3q:Path,"
					" HardLink=%4q:HardLink,"
					" CreateTime=NOW(),"
					" AccessTime=NOW()"
					"ON DUPLICATE KEY UPDATE"
					" Path=%3q:Path,"
					" HardLink=%4q:HardLink,"
					" CreateTime=NOW(),"
					" AccessTime=NOW()"
					;
			query.parse();

			mysqlpp::SQLQueryParms qp;
			qp << org_id << identifier << file_format << path << (is_hardlink? "yes": "no");
			query.execute(qp);
		}
		catch(const mysqlpp::BadQuery& e) {
			LogCritical("Bad Query: "<<e.what());
			throw;
		}
		catch(const mysqlpp::Exception& e) {
			LogError("Exception: "<<e.what());
			throw;
		}
	}
};

}  // namespace database

}  // namespace app

#endif /* DATABASE_HPP_ */
