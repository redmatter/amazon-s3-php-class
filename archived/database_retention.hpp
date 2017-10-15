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
		archiving::file_type::file_type_t file_type;
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
		archiving::file_type::file_type_t filetype;
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
			" `Purged`, `RetentionMaxDate` <= CURDATE() /*NOT-USED*/, ID ";
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
							archiving::file_type::file_type_t(rml::utils::strtoull(row[3].c_str())),
							rml::utils::strtoull(row[4].c_str()), row[0].c_str()),// Identifier
					types::file_format::file_format_t(rml::utils::strtoull(row[2].c_str())),
					archiving::user_id_t(rml::utils::strtoull(row[5].c_str())),   // UserID
					policy::id_type(rml::utils::strtoull(row[6].c_str())),      // PolicyID
					policy::version_type(rml::utils::strtoull(row[7].c_str())), // PolicyVersion
					creation_date, retention_min_date, retention_max_date,
					rml::utils::strtoull(row[17].c_str()),
					row[19] == "yes",
					rml::utils::strtoull(row[18].c_str()),
					rml::utils::strtoull(row[21].c_str())));
		}

		return ret;
	}

	template < typename Iter, typename ExtractIdentifier >
	bool purge_audit(rml::MySQLConnection& conn, const archiving::org_id_t& org,
			const coreapi_eventlog::id_t& eventlog_id, Iter begin, Iter end,
			const boost::function< bool(const archiving::archivable::identifier&) >& fn_is_filetype_auto_expired,
			const std::string& auditable_action)
	{
		if(begin == end)
			return true;

		try
		{
			ExtractIdentifier get_identifier;

			bool has_items_to_audit(false);

			mysqlpp::Query query = conn->query();
			query << "INSERT INTO Audit_"<<org<<" ( `OrgID`, `Type`, `UUID`, `Action`, `Time`, `AuditLogID` ) VALUES \n";
			for(Iter itr = begin; itr != end; ++itr)
			{
				const archiving::archivable::identifier& id(get_identifier(*itr));

				if(fn_is_filetype_auto_expired(id))
					continue;

				has_items_to_audit = true;

				std::string uuid_hex(id.uuid);
				boost::algorithm::erase_all(uuid_hex, "-");

				query << (itr == begin? ' ': ',') << '(' << org << ',' << int(id.filetype) << ',' << "0x"<<uuid_hex << ", '"<<auditable_action<<"', NOW(), " << eventlog_id << ")\n";
			}

			if(!has_items_to_audit)
				return true;

			__SQL_DEBUG(query.str());

			query.execute();

			return true;
		}
		catch(const std::exception& e) {
			LogWarning("Failed to save Audit - err:"<<e.what());
		}

		return false;
	}

	int get_retention_for_purge_ondemand(
			rml::coreapi& api, const archiving::org_id_t& org_id,
			const boost::function< bool(retention_record::pointer) >& is_purgeaple,
			const boost::function< bool(const archiving::archivable::identifier&) >& fn_is_filetype_auto_expired,
			const boost::function< void(const retention_record::set_t&, const uint64_t&, const uint64_t&) >& fn_schedule_purgeables)
	{
		int count(0);

		rml::MySQLConnection conn(*p_pool_);
		mysqlpp::Transaction transaction(*conn);
		{
			mysqlpp::Query query = conn->query();
			query << "SET time_zone = '+00:00'";
			query.execute();
		}

		{
			mysqlpp::Query query = conn->query();
			query << "SELECT `dr`.`ID`, `dr`.`AuditLogID`, GROUP_CONCAT( `dri`.`RetentionID` SEPARATOR ',' ) "
					"FROM `DeleteRequest_"<<org_id<<"` `dr` "
					"LEFT JOIN `DeleteRequestItems_"<<org_id<<"` `dri` ON `dr`.`ID` = `dri`.`DeleteRequestID` "
					"WHERE `dr`.`State`='Pending' "
					"FOR UPDATE";
			__SQL_DEBUG(query.str());
			 mysqlpp::StoreQueryResult res = query.store();
			const size_t& n_rows(res.num_rows());
			if(!n_rows || (res[0][0] == "NULL"))
				return 0;

			for(size_t i_row = 0; i_row<n_rows; ++i_row) {

				unsigned int not_purgeable_items(0);

				mysqlpp::Row row = res[i_row];

				retention_record::set_t requested_items;

				uint64_t delete_request_id(rml::utils::strtoull(row[0].c_str()));
				coreapi_eventlog::id_t eventlog_id(rml::utils::strtoull(row[1].c_str()));
				coreapi_eventlog::pointer p_eventlog(coreapi_eventlog::find(api, std::make_pair(org_id, eventlog_id)));
				if(!p_eventlog)
					THROW_RML_EXCEPTION("Failed to verify eventlog ID:"<<eventlog_id);

				//fill requested_items
				{
					rml::MySQLConnection conn(*p_pool_);

					mysqlpp::Query query = conn->query();
					query << "SELECT "<<retention_select_fields()<<" FROM Retention_"<<org_id<<" WHERE ID IN ( "<<row[0].c_str()<<" ) AND `State`='Available'";
					__SQL_DEBUG(query.str());

					mysqlpp::UseQueryResult res = query.use();
					while(retention_record::pointer rec = make_retention_record(res.fetch_row())) {
						if(!is_purgeaple(rec)) {
							not_purgeable_items++;
							LogVerbose(rec->identifier<<" : Not yet purgeable");
							continue;
						}

						requested_items.insert(rec);
						count++;
					}
				}

				if(!requested_items.empty()) {
					// audit them
					purge_audit< retention_record::set_t::iterator, retention_record::extract_identifier >(
							conn, org_id, p_eventlog->id, requested_items.begin(), requested_items.end(),
							fn_is_filetype_auto_expired, "Purge:OnDemand");

					// update retention records to set State to PurgeQueued
					rml::MySQLConnection conn_update_retention(*p_pool_);
					mysqlpp::Query query_update_retention = conn_update_retention->query();
					query_update_retention << "UPDATE `ArchivingRetention`.`Retention_"<<org_id<<"` SET `State`='PurgeQueued' WHERE ID IN(";
					retention_record::set_t::const_iterator begin(requested_items.begin());
					retention_record::set_t::const_iterator end(requested_items.end());
					for(retention_record::set_t::const_iterator itr = begin; itr != end; ++itr)
						query_update_retention << (itr == begin? ' ': ',')<<(*itr)->id;
					query_update_retention << ')';
					__SQL_DEBUG(query_update_retention.str());
					query_update_retention.execute();

					// schedule the found purge-able items
					fn_schedule_purgeables(requested_items, p_eventlog->id, delete_request_id);
				}
				else
					LogDWarning("No records found to purge, for delete request ID:"<<delete_request_id, "IGNO");

				if(!not_purgeable_items) {
					mysqlpp::Query query = conn->query();
					query << "UPDATE `DeleteRequest_"<<org_id<<"` SET `State`='Done' WHERE `ID`="<<delete_request_id;
					__SQL_DEBUG(query.str());
					query.execute();
				}
				else
					LogVerbose("DeleteRequest:"<<delete_request_id<<" not set Done as it has a few pending items");
			}
		}

		transaction.commit();

		return count;
	}

	uint64_t get_retention_for_purge(
			rml::coreapi& api, const archiving::org_id_t& org_id,
			const uint64_t& max_retention_id, const unsigned int limit,
			const std::string& where_configured_restrictions,
			const boost::function< bool(retention_record::pointer) >& is_purgeaple,
			const boost::function< bool(const archiving::archivable::identifier&) >& fn_is_filetype_auto_expired,
			const bool& org_had_legacy_ondemand_purge,
			const boost::function< void(const retention_record::set_t&, const uint64_t&, const uint64_t&) >& fn_schedule_purgeables)
	{
		uint64_t new_max_retention_id = max_retention_id;
		retention_record::set_t retention_set;

		rml::MySQLConnection conn(*p_pool_);
		mysqlpp::Transaction transaction(*conn);

		{
			mysqlpp::Query query = conn->query();
			query << "SET time_zone = '+00:00'";
			query.execute();
		}

		unsigned int limit_remaining(limit);
		unsigned int max_retry(10);
		do {
			mysqlpp::Query query = conn->query();
			query << "SELECT "
					<< retention_select_fields() <<
					"FROM `ArchivingRetention`.`Retention_"<<org_id<<"` r "
					"WHERE ID > "<<new_max_retention_id<<" AND `State`='Available' "<<where_configured_restrictions<<" "
					"LIMIT "<<limit_remaining<<" FOR UPDATE";

			__SQL_DEBUG(query.str());

			mysqlpp::UseQueryResult res = query.use();
			while(retention_record::pointer rec = make_retention_record(res.fetch_row())) {
				if(is_purgeaple(rec)) {
					limit_remaining--;
					retention_set.insert(rec);
				}

				new_max_retention_id = std::max(new_max_retention_id, rec->id);
			}
		} while(limit_remaining && --max_retry);

		// here look for any object that would fit legacy on-demand purge criteria.
		if (org_had_legacy_ondemand_purge && max_retention_id < new_max_retention_id) {
			mysqlpp::Query query = conn->query();
			query << "SELECT " << retention_select_fields() << " "
					"FROM `ArchivingRetention`.`Retention_"<<org_id<<"` r "
					"WHERE (ID > "<<max_retention_id<<" AND ID <= "<<new_max_retention_id<<" AND `State`='PurgeQueued' AND `Purged`='yes')"
					" "<<where_configured_restrictions<<" "
					"FOR UPDATE";

			__SQL_DEBUG(query.str());

			mysqlpp::UseQueryResult res = query.use();
			while(retention_record::pointer rec = make_retention_record(res.fetch_row())) {
				retention_set.insert(rec);
			}
		}

		if(!retention_set.empty()) {
			// Update state
			{
				mysqlpp::Query query = conn->query();
				query << "UPDATE `ArchivingRetention`.`Retention_"<<org_id<<"` SET `State`='PurgeQueued', `Purged`='no' WHERE ID IN(";
				retention_record::set_t::const_iterator begin(retention_set.begin());
				retention_record::set_t::const_iterator end(retention_set.end());
				for(retention_record::set_t::const_iterator itr = begin; itr != end; ++itr)
					query << (itr == begin? ' ': ',')<<(*itr)->id;
				query << ')';

				__SQL_DEBUG(query.str());
				query.execute();
			}

			// make audit record
			coreapi_eventlog::pointer eventlog = coreapi_eventlog::create(
					api, org_id, coreapi_eventlog::levels::message, "Purge based on retention rules", "Archiving",
					"Purge:Auto");
			purge_audit< retention_record::set_t::iterator, retention_record::extract_identifier >(
					conn, org_id, eventlog->id, retention_set.begin(), retention_set.end(), fn_is_filetype_auto_expired,
					eventlog->action);

			// schedule the found purge-able items
			fn_schedule_purgeables(retention_set, eventlog->id, 0 /* not on-demand purge */);

			transaction.commit();
		}

		return new_max_retention_id;
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

			mysqlpp::Query query = conn->query();
			query << "SELECT OrgID FROM ("
					" SELECT SUBSTRING(TABLE_NAME FROM 7) `OrgID` FROM `information_schema`.TABLES WHERE TABLES.TABLE_SCHEMA='ArchivingRetention' AND TABLE_NAME LIKE 'Usage_%' AND TABLE_NAME != 'Usage_Template'"
					") X WHERE ( X.OrgID % 47 ) % @@auto_increment_increment + 1 = @@auto_increment_offset";

			__SQL_DEBUG(query.str());

			mysqlpp::UseQueryResult res = query.use();
			while (mysqlpp::Row row = res.fetch_row()) {
				const archiving::org_id_t org(rml::utils::strtoull(row[0].c_str()));
				ret->set.insert(
						types::purge_def::org_def(org));
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

	bool usage_purge(const archiving::org_id_t& org, const archiving::file_type::file_type_t& filetype, const uint64_t& endpoint_id, const int& count, const int64_t& clear_usage, const int64_t& real_usage)
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

			__SQL_DEBUG(query.str());
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

	uint64_t count_purge_audit_records(const archiving::archivable::identifier& identifier)
	{
		rml::MySQLConnection conn(*p_pool_);
		try {
			mysqlpp::Query query = conn->query();

			std::string uuid_hex(identifier.uuid);
			boost::algorithm::erase_all(uuid_hex, "-");
			query << "SELECT COUNT(*) FROM ArchivingRetention.Audit_"<<identifier.org<<" "
					"WHERE Action LIKE 'Purge:%'"
					" AND UUID=0x"<<uuid_hex<<
					" AND Type="<<static_cast<int>(identifier.filetype);
			__SQL_DEBUG(query.str());

			return query.use().fetch_row().at(0);
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

		return 0;
	}

	template<typename Iter, typename ExtractRetentionID>
	inline bool
	set_purged(const archiving::org_id_t &org, const Iter &begin, const Iter &end, const ExtractRetentionID &extract_id)
	{
		if (begin == end) {
			LogWarning("Empty set given; ignored!");
			return true;
		}

		rml::MySQLConnection conn(*p_pool_);
		mysqlpp::Transaction transaction(*conn, false);
		try {
			mysqlpp::Query query = conn->query();
			query << "UPDATE ArchivingRetention.Retention_"<<org<<" SET State='Purged' WHERE ID IN (";

			bool first_one = true;
			for(Iter itr = begin; itr != end; ++itr) {
				query << (first_one?' ':',')<<extract_id(*itr);
				first_one = false;
			}
			query << " )";

			__SQL_DEBUG(query.str());
			query.execute();
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

						std::string uuid_hex(itr->uuid);
						boost::algorithm::erase_all(uuid_hex, "-");

						query << ((itr != batch_range.first)? ',': '\n')
								<<"( "
								<<"0x"<<uuid_hex<<" /* "<<itr->uuid<<" */, ";

						if(!itr->leg_uuid.empty()) {
							std::string uuid_hex(itr->leg_uuid);
							boost::algorithm::erase_all(uuid_hex, "-");
							query << "0x"<<uuid_hex<<" /* "<<itr->leg_uuid<<" */, ";
						}
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
};

}  // namespace database

}  // namespace app

#endif /* DATABASE_RETENTION_HPP_ */
