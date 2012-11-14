/*
 * database.hpp
 *
 *  Created on: 7 Mar 2011
 *      Author: dkorah
 */

#ifndef DATABASE_RETENTION_HPP_
#define DATABASE_RETENTION_HPP_

#include "operation_data.hpp"
#include "types.hpp"
#include "retention_record.hpp"

#include "../common/policy.hpp"
#include "../common/basic_database.hpp"

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
			inline_ bool operator()(const data& lhs, const data& rhs) const
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
			inline_ bool operator()(const usage_data& lhs, const usage_data& rhs) const
			{
				return lhs.org < rhs.org;
			}
		};

		typedef std::multiset<usage_data, usage_data::compare> multiset_t;
	};

	static inline_ const char* get_identifier()
	{
		static const char* identifier = "retention";
		return identifier;
	}

	retention(const boost::program_options::variables_map& confMap) :
		basic_database< retention >(confMap)
	{
	}

	static inline_ const char* retention_select_fields()
	{
		return  ""
			" LCASE(CONCAT(HEX(SUBSTR(UUID, 1, 4)),'-',HEX(SUBSTR(UUID, 5, 2)),'-',HEX(SUBSTR(UUID, 7, 2)),'-',HEX(SUBSTR(UUID, 9, 2)),'-',HEX(SUBSTR(UUID, 11)))) `UUID`,"
			" LCASE(CONCAT(HEX(SUBSTR(LegUUID, 1, 4)),'-',HEX(SUBSTR(LegUUID, 5, 2)),'-',HEX(SUBSTR(LegUUID, 7, 2)),'-',HEX(SUBSTR(LegUUID, 9, 2)),'-',HEX(SUBSTR(LegUUID, 11)))) `LegUUID`,"
			" `Format`, `Type`, `OrgID`, `UserID`, `PolicyID`, `PolicyVersion`,"
			" DAYOFMONTH(`CreationDate`), MONTH(`CreationDate`), YEAR(`CreationDate`),"
			" DAYOFMONTH(`RetentionMinDate`), MONTH(`RetentionMinDate`), YEAR(`RetentionMinDate`),"
			" DAYOFMONTH(`RetentionMaxDate`), MONTH(`RetentionMaxDate`), YEAR(`RetentionMaxDate`),"
			" `MidnightOffsetSeconds`, `EndpointID`,"
			" `Purged`, `RetentionMaxDate` <= CURDATE(), ID ";
	}

	static inline_ retention_record::pointer make_retention_record(const mysqlpp::Row& row)
	{
		retention_record::pointer ret;
		if(row) {
			const applied_policy::ymd_t creation_date(                               // CreationDate
					rml::utils::strtoull(row[10].c_str()),
					rml::utils::strtoull(row[9].c_str()),
					rml::utils::strtoull(row[8].c_str()));
			const applied_policy::ymd_t retention_min_date(                          // RetentionMinDate
					rml::utils::strtoull(row[13].c_str()),
					rml::utils::strtoull(row[12].c_str()),
					rml::utils::strtoull(row[11].c_str()));
			const applied_policy::ymd_t retention_max_date(                          // RetentionMinDate
					rml::utils::strtoull(row[16].c_str()),
					rml::utils::strtoull(row[15].c_str()),
					rml::utils::strtoull(row[14].c_str()));

			ret.reset(new retention_record(
					archiving::archivable::identifier(
							archiving::archivable::file_type::file_type_t(rml::utils::strtoull(row[3].c_str())),
							rml::utils::strtoull(row[4].c_str()), row[0].c_str()),// Identifier
					types::file_format::file_format_t(rml::utils::strtoull(row[2].c_str())),
					archiving::user_id_t(rml::utils::strtoull(row[5].c_str())),   // UserID
					policy::id_type(rml::utils::strtoull(row[6].c_str())),      // PolicyID
					policy::version_type(rml::utils::strtoull(row[7].c_str())), // PolicyVersion
					creation_date, retention_min_date, retention_max_date,
					rml::utils::strtoull(row[17].c_str()),
					row[19] == "yes",
					row[20] == "1",
					rml::utils::strtoull(row[18].c_str()),
					rml::utils::strtoull(row[21].c_str())));
		}

		return ret;
	}

	retention_record::set_t get_retention_for_purge(const archiving::org_id_t& org_id, const uint64_t& max_retention_id,
			const unsigned int limit, const std::string& where_configured_restrictions)
	{
		retention_record::set_t ret;

		rml::MySQLConnection conn(*p_pool_); // scoped connection
		mysqlpp::Transaction transaction(*conn);

		{
			mysqlpp::Query query = conn->query();
			query << "SELECT "
					<< retention_select_fields() <<
					"FROM `ArchivingRetention`.`Retention_"<<org_id<<"` r "
					"WHERE ID > "<<max_retention_id<<" AND `State`='Available' "<<where_configured_restrictions<<" "
					"LIMIT "<<limit<<" FOR UPDATE";

			SQL_DEBUG(query.str());

			mysqlpp::UseQueryResult res = query.use();
			while(retention_record::pointer rec = make_retention_record(res.fetch_row()))
				ret.insert(rec);
		}

		if(!ret.empty()) {
			mysqlpp::Query query = conn->query();
			query << "UPDATE `ArchivingRetention`.`Retention_"<<org_id<<"` SET `State`='PurgeQueued' WHERE ID IN(";
			retention_record::set_t::const_iterator begin(ret.begin());
			retention_record::set_t::const_iterator end(ret.end());
			for(retention_record::set_t::const_iterator itr = begin; itr != end; ++itr)
				query << (itr == begin? ' ': ',')<<(*itr)->id;
			query << ')';

			SQL_DEBUG(query.str());
			query.execute();
		}

		transaction.commit();

		return ret;
	}

	retention_record::pointer get_retention(const archiving::archivable::identifier& identifier)
	{
		try {
			std::string uuid_hex(identifier.uuid);
			boost::algorithm::erase_all(uuid_hex, "-");
			
			rml::MySQLConnection conn(*p_pool_); // scoped connection
			mysqlpp::Query query = conn->query();
			query << "SELECT"
					<< retention_select_fields() <<
					"FROM `ArchivingRetention`.`Retention_"<<identifier.org<<"` "
					"WHERE `UUID`=0x"<<uuid_hex<<" /*"<<identifier.uuid<<"*/ AND `Type`="<<mysqlpp::sql_int(identifier.filetype)<<" " // comments for easy grepping in logs; hex for performance
					"LIMIT 1";

			SQL_DEBUG(query.str());
			
			mysqlpp::UseQueryResult res = query.use();
			return make_retention_record(res.fetch_row());
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

		return retention_record::pointer();
	}

	types::purge_def::pointer get_orgs_for_purge()
	{
		types::purge_def::pointer ret(new types::purge_def(time(0)));

		try {
			rml::MySQLConnection conn(*p_pool_); // scoped connection

			static unsigned int auto_increment_offset(0);
			static unsigned int auto_increment_increment(0);
			if(!auto_increment_increment) {
				mysqlpp::Query query = conn->query();
				query << "SELECT @@auto_increment_offset, @@auto_increment_increment";

				SQL_DEBUG(query.str());

				mysqlpp::UseQueryResult res = query.use();
				while (mysqlpp::Row row = res.fetch_row()) { // only once
					auto_increment_increment = rml::utils::strtoull(row[1].c_str(), 0ULL);
					auto_increment_offset = rml::utils::strtoull(row[0].c_str(), 0ULL) % auto_increment_increment;
				}

				if(!auto_increment_increment)
					throw rml::exception("Failed to figure out auto_increment_XXX");
			}

			mysqlpp::Query query = conn->query();
			query << "SELECT TABLE_NAME FROM `information_schema`.TABLES WHERE TABLES.TABLE_SCHEMA='ArchivingRetention' AND TABLE_NAME LIKE 'Retention_%' AND TABLE_NAME != 'Retention_Template'";

			SQL_DEBUG(query.str());

			mysqlpp::UseQueryResult res = query.use();
			while (mysqlpp::Row row = res.fetch_row()) {
				const archiving::org_id_t org(rml::utils::strtoull(row[0].c_str() + 10));
				if ((org % auto_increment_increment) == auto_increment_offset)
					ret->set.insert(types::purge_def::org_def(org));
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

		return ret;
	}

	bool usage_purge(const archiving::org_id_t& org, const archiving::archivable::file_type::file_type_t& filetype, const uint64_t& endpoint_id, const int& count, const int64_t& clear_usage, const int64_t& real_usage)
		__attribute__ ((warn_unused_result))
	{
		try {
			rml::MySQLConnection conn(*p_pool_); // scoped connection
			{
				mysqlpp::Query query = conn->query();
				query << "SET time_zone = '+00:00'";
				query.execute();
			}

			mysqlpp::Query query = conn->query();
			query << "INSERT INTO `ArchivingRetention`.`Usage_"<<org<<"` \n"
					"( `Hour`, `Type`, `OrgID`, `Count`, `ClearUsage`, `RealUsage`, `EndpointID` )"
					" VALUES ("
					<< " DATE_ADD(CURDATE(), INTERVAL HOUR(NOW()) HOUR), "
					<< '\''<<filetype<<"', "
					<< org<<", "
					<< count<<", "
					<< clear_usage<<", "
					<< real_usage<<", "
					<< endpoint_id
					<< " ) "
					<< "ON DUPLICATE KEY UPDATE `Count`=`Count`+VALUES(`Count`), `ClearUsage`=`ClearUsage`+VALUES(`ClearUsage`), `RealUsage`=`RealUsage`+VALUES(`RealUsage`)"
					;

			SQL_DEBUG(query.str());
			query.execute();

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

	template < typename Iter, typename ExtractRetentionID, typename IsFileTypeCDR >
	inline bool set_purged(const archiving::org_id_t& org, Iter begin, Iter end, const bool& delete_cdrs)
	{
		try {
			ExtractRetentionID extract_id;
			IsFileTypeCDR is_cdr;

			rml::MySQLConnection conn(*p_pool_);

			unsigned int cdrs_found(0);
			bool noncdrs_found(false);
			mysqlpp::Query query_bin_cdrs = conn->query();
			query_bin_cdrs << "DELETE FROM ArchivingRetention.Retention_"<<org<<" WHERE Type="<<is_cdr.filetype_as_int()<<" AND ID IN (";

			mysqlpp::Query query_update_noncdrs = conn->query();
			query_update_noncdrs << "UPDATE ArchivingRetention.Retention_"<<org<<" SET State='Purged' WHERE ID IN (";

			for(Iter itr = begin; itr != end; ++itr) {
				if(delete_cdrs && is_cdr(*itr)) {
					query_bin_cdrs << (cdrs_found?',':' ')<<extract_id(*itr);
					cdrs_found++;
				}
				else {
					query_update_noncdrs << (noncdrs_found?',':' ')<<extract_id(*itr);
					noncdrs_found = true;
				}
			}

			query_bin_cdrs << " )";
			query_update_noncdrs << " )";

			if(delete_cdrs && cdrs_found) {
				SQL_DEBUG(query_bin_cdrs.str());
				mysqlpp::SimpleResult res = query_bin_cdrs.execute();
				assert(cdrs_found == res.rows());
			}

			if(noncdrs_found) {
				SQL_DEBUG(query_update_noncdrs.str());
				query_update_noncdrs.execute();
			}

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
	void set_purged(const retention_record::set_t& rets)
	{
		retention_record::filetype_grouped_set_t grouped;

		{
			const retention_record::set_t::const_iterator& begin(rets.begin());
			const retention_record::set_t::const_iterator& end(rets.end());

			if(begin == end)
				return;

			for(retention_record::set_t::const_iterator itr = begin; itr != end; ++itr)
				grouped.insert(*itr);
		}

		rml::MySQLConnection conn(*p_pool_); // scoped connection
		mysqlpp::Transaction transaction(*conn, false);

		try {

			mysqlpp::Query query = conn->query();

			const retention_record::filetype_grouped_set_t::const_iterator& end(grouped.end());

			retention_record::filetype_grouped_set_t::const_iterator batch_itr = grouped.begin();
			const archiving::org_id_t& org((*batch_itr)->identifier.org);
			while(batch_itr != end) {
				std::pair< retention_record::filetype_grouped_set_t::const_iterator, retention_record::filetype_grouped_set_t::const_iterator > batch_range =
						grouped.equal_range(*batch_itr);
				const archiving::archivable::file_type::file_type_t& filetype((*batch_itr)->identifier.filetype);

				mysqlpp::Query query = conn->query();
				query << "UPDATE `ArchivingRetention`.`Retention_"<<org<<"` \n"
						" SET Purged='yes' "
						"WHERE Type="<<filetype<<" AND UUID IN (";

				for(retention_record::filetype_grouped_set_t::const_iterator itr = batch_range.first; itr != batch_range.second; ++itr) {
					std::string uuid_hex((*itr)->identifier.uuid);
					boost::algorithm::erase_all(uuid_hex, "-");
					query << ((itr != batch_range.first)? ',': '\n') << "0x"<<uuid_hex << " /""* "<<(*itr)->identifier.uuid<<" *""/ ";
				}
				query << ')';

				SQL_DEBUG(query.str());

				mysqlpp::SimpleResult res = query.execute();
				LogVerbose("Retention data: "<<res.rows()<<" rows purged for Org:"<<org);

				batch_itr = batch_range.second;
			}

			transaction.commit();
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
	}
	*/

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
				while(batch_itr != end) {
					std::pair< data::multiset_t::const_iterator, data::multiset_t::const_iterator > batch_range = data.equal_range(*batch_itr);
					const archiving::org_id_t& org(batch_itr->org);

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

					SQL_DEBUG(query.str());

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

					SQL_DEBUG(query.str());

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

}  // namespace database

}  // namespace app

#endif /* DATABASE_RETENTION_HPP_ */
