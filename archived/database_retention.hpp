//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 07/03/2011.
//

#ifndef DATABASE_RETENTION_HPP_
#define DATABASE_RETENTION_HPP_

#include "operation_data.hpp"
#include "types.hpp"
#include "retention_record.hpp"
#include "macros.h"

#include "../common/policy.hpp"
#include "../common/coreapi_eventlog.hpp"
#include "config/purge_restrictions.hpp"

#include <rml/database/mysql/basic_database.hpp>
#include <rml/database/mysql/transactional_connection.hpp>
#include <rml/database/mysql/errors.hpp>
#include <rml/database/mysql/utils.hpp>
#include <rml/exception.h>
#include <rml/logger.h>

#include <mysql/mysqld_error.h>

#include <boost/program_options.hpp>
#include <boost/thread/thread.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/algorithm/string/erase.hpp>

#include <set>

#include <time.h>

#define MYSQL_GROUP_CONCAT_BUF_LENGTH 102400

namespace app {

extern std::string hostname;

namespace database {

namespace rmldb = rml::database::mysql;

class retention : public rmldb::basic_database<retention, rmldb::transactional_connection>
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
		archiving::file_type::file_type_t filetype;
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
		return "retention";
	}

	explicit retention(const boost::program_options::variables_map& confMap) :
			rmldb::basic_database<retention, rmldb::transactional_connection>(confMap)
	{
	}

	static inline const char* retention_select_fields()
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

	static inline retention_record::pointer make_retention_record(const mysqlpp::Row& row)
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
							archiving::file_type::file_type_t(static_cast<int>(row[3])),
							static_cast<archiving::org_id_t>(row[4]), row[0].c_str()),// Identifier
					types::file_format::file_format_t(rml::utils::strtoull(row[2].c_str())),
					archiving::user_id_t(rml::utils::strtoull(row[5].c_str())),   // UserID
					policy::id_type(rml::utils::strtoull(row[6].c_str())),      // PolicyID
					policy::version_type(rml::utils::strtoull(row[7].c_str())), // PolicyVersion
					creation_date, retention_min_date, retention_max_date,
					static_cast<std::time_t>(row[17]),
					row[19] == "yes",
					rml::utils::strtoull(row[18].c_str()),
					rml::utils::strtoull(row[21].c_str())));
		}

		return ret;
	}

	template <typename ExtractIdentifier, typename Iter>
	bool purge_audit(const boost::shared_ptr<rmldb::transactional_connection>& conn, const archiving::org_id_t& org,
			const coreapi_eventlog::id_t& eventlog_id, Iter begin, Iter end,
			const boost::function< bool(const archiving::archivable::identifier&) >& fn_is_filetype_auto_expired,
			const std::string& auditable_action,
			const boost::function< boost::optional<bool>(const archiving::file_type::file_type_t&) >& is_internal_filetype = boost::function< boost::optional<bool>(const archiving::file_type::file_type_t&) >())
	{
		if(begin == end)
			return true;

		try
		{
			ExtractIdentifier get_identifier;

			for(Iter itr = begin; itr != end; ++itr)
			{
				const archiving::archivable::identifier& id(get_identifier(*itr));

				if(fn_is_filetype_auto_expired(id))
					continue;

				// it can be empty in the case of invocation from "on-demand" purge
				if (!is_internal_filetype.empty()) {
					boost::optional<bool> is_internal = is_internal_filetype(id.filetype);
					// The optional-bool will evaluate to true only if the it contains a true.
					// The implementation of the function returns a boost::none if the config-option is set to OFF
					if (is_internal == true) {
						continue;
					}
				}

				std::string uuid_hex(id.uuid);
				boost::algorithm::erase_all(uuid_hex, "-");

				mysqlpp::Query query = conn->query();
				query << "/* audit "<<id<<' '<<auditable_action<<" */ "
						"INSERT INTO `Audit_"<<org<<"` (`OrgID`, `Type`, `UUID`, `Action`, `Time`, `AuditLogID`)"
						"    SELECT "<<org<<", "<<int(id.filetype)<<", 0x"<<uuid_hex<<", '"<<auditable_action<<"', NOW(), "<<eventlog_id<<
						"        FROM `Audit_"<<org<<"` WHERE NOT EXISTS ("
						"            SELECT ID FROM `Audit_"<<org<<"` WHERE Type="<<int(id.filetype)<<" AND UUID=0x"<<uuid_hex<<" AND Action='"<<auditable_action<<"' LIMIT 1"
						"        ) LIMIT 1";
				MYSQL_QUERY_EXECUTE(PURGE, conn, query);
			}

			return true;
		}
		catch (const mysqlpp::BadQuery& e) {
			LogError("MySQL Error: " << e.what());
			// re-throw this exception in case the connection is stale and need to be thrown away, which is handled by
			// the caller
			throw;
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

		try {
			typedef mysqlpp::StoreQueryResult::const_iterator row_iterator;

			boost::shared_ptr<rmldb::transactional_connection> conn = get_connection_wait_for(boost::chrono::seconds(5));

			mysqlpp::StoreQueryResult delete_requests;
			{
				mysqlpp::Query query = conn->query(
						"SELECT `dr`.`ID`, `dr`.`AuditLogID` FROM `DeleteRequest_%0:OrgID` `dr` "
						"WHERE `dr`.`State`='ReadyForPurge' LIMIT 10");
				query.parse();
				mysqlpp::SQLQueryParms qp;
				qp << org_id;

				MYSQL_QUERY_STORE(PURGE, conn, query, delete_requests_, qp);
				delete_requests = delete_requests_;
			}

			const unsigned limit = 1000;
			for (row_iterator itr = delete_requests.begin(); itr != delete_requests.end(); ++itr) {
				const uint64_t delete_request_id(itr->at(0));
				const coreapi_eventlog::id_t eventlog_id(itr->at(1));

				if(!coreapi_eventlog::find(api, std::make_pair(org_id, eventlog_id))) {
					LogWarning("Failed to verify eventlog ID:"<<eventlog_id);
					continue;
				}

				conn->start_transaction();

				std::list<uint64_t> retention_ids;
				{
					mysqlpp::Query query = conn->query(
							"SELECT `RetentionID` "
							"FROM `DeleteRequestItems_%0:OrgID` "
							"WHERE `State`='Approved'"
							" AND `DeleteRequestID` = %1:DeleteRequestID "
							"LIMIT %2:Limit");
					query.parse();
					mysqlpp::SQLQueryParms qp;
					qp << org_id << delete_request_id << limit;

					MYSQL_QUERY_STORE(PURGE, conn, query, items, qp);
					{
						const row_iterator& items_begin(items.begin());
						const row_iterator& items_end(items.end());
						for (row_iterator items_itr = items_begin; items_itr != items_end; ++items_itr) {
							retention_ids.push_back(items_itr->at(0));
						}
					}
				}

				bool some_did_not_make_it = false;

				//fill requested_items
				if (!retention_ids.empty()) {
					retention_record::set_t requested_items;

					const std::list<uint64_t>::const_iterator& items_begin(retention_ids.begin());
					const std::list<uint64_t>::const_iterator& items_end(retention_ids.end());
					for (std::list<uint64_t>::const_iterator items_itr = items_begin; items_itr != items_end; ++items_itr) {
						mysqlpp::StoreQueryResult retention_res;
						{
							mysqlpp::Query query = conn->query();
							query << "SELECT " << retention_select_fields()
								  << "FROM `Retention_" << org_id << "` WHERE ID = " << *items_itr
								  << " AND `State`='Available' LIMIT 1 FOR UPDATE";
							MYSQL_QUERY_STORE(PURGE, conn, query, retention_res_);
							retention_res = retention_res_;
						}

						if (!retention_res.empty()) {
							const retention_record::pointer& rec(make_retention_record(retention_res[0]));
							if (!is_purgeaple(rec)) {
								LogVerbose(rec->identifier<<" : Not yet purgeable");
								some_did_not_make_it = true;
								continue;
							}

							// update retention record; set State to PurgeQueued
							{
								mysqlpp::Query query = conn->query(
										"UPDATE `Retention_%0:OrgID` SET `State`='PurgeQueued' "
										"WHERE ID=%1:RetentionID LIMIT 1");
								mysqlpp::SQLQueryParms qp;
								qp << org_id << *items_itr;
								MYSQL_QUERY_EXECUTE(PURGE, conn, query, qp);
							}

							count++;
							requested_items.insert(rec);
						}
					}

					purge_audit<retention_record::extract_identifier>(
							conn, org_id, eventlog_id, requested_items.begin(), requested_items.end(),
							fn_is_filetype_auto_expired, "Purge:OnDemand");

					// schedule the found purge-able items
					fn_schedule_purgeables(requested_items, eventlog_id, delete_request_id);
				}

				// looks like we have loaded every item from the request; if there were only <limit> number of records
				// the request will be marked Done in the next sweep.
				if (!some_did_not_make_it && retention_ids.size() < limit) {
					mysqlpp::Query query = conn->query(
							"UPDATE `DeleteRequest_%0:OrgID` SET `State`='Done' "
							"WHERE `ID`=%1:DeleteRequestID "
							"LIMIT 1");
					query.parse();
					mysqlpp::SQLQueryParms qp;
					qp << org_id << delete_request_id;
					MYSQL_QUERY_EXECUTE(PURGE, conn, query, qp);
				}

				conn->commit_transaction();
			}
		}
		MYSQL_CATCH_LOG_ERROR(e)

		return count;
	}

	uint64_t
	get_max_retention_id(const boost::shared_ptr<rmldb::transactional_connection>& conn, const archiving::org_id_t org_id)
	{
		mysqlpp::Query query = conn->query();
		query << "SELECT `AUTO_INCREMENT` FROM INFORMATION_SCHEMA.TABLES "
				 "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'Retention_%0:OrgID'";
		query.parse();
		mysqlpp::SQLQueryParms qp;
		qp << org_id;
		MYSQL_QUERY_STORE(PURGE, conn, query, res, qp);
		return res.at(0).at(0);
	}

	uint64_t get_retention_for_purge(
			rml::coreapi& api, const archiving::org_id_t& org_id,
			const uint64_t& max_retention_id, const unsigned int limit,
			const archiving::archived::purge_restrictions& restrictions,
			const boost::function< bool(retention_record::pointer) >& is_purgeaple,
			const boost::function< bool(const archiving::archivable::identifier&) >& fn_is_filetype_auto_expired,
			const boost::function< boost::optional<bool>(const archiving::file_type::file_type_t&) >& is_internal_filetype,
			const bool& org_had_legacy_ondemand_purge,
			const boost::function< void(const retention_record::set_t&, const uint64_t&, const uint64_t&) >& fn_schedule_purgeables)
	{
		try {
			uint64_t new_max_retention_id = max_retention_id;
			retention_record::set_t retention_set;

			boost::shared_ptr<rmldb::transactional_connection> conn = get_connection_wait_for(boost::chrono::seconds(1));
			conn->start_transaction();

			const uint64_t max_id = get_max_retention_id(conn, org_id);
			LogVerbose("Org: " << org_id << " Max retention-id: " << max_id);

			unsigned attempts = 5;
			for (uint64_t range_begin = max_retention_id, range_end = 0;
				 retention_set.empty() && range_begin < max_id && attempts--;
				 range_begin = range_end) {

				range_end = (range_begin + std::max(limit * 10, 10000U));
				LogVerbose("Org: " << org_id << " Scanning for purgeables "
												"(retention id range " << range_begin << " - " << range_end << ")");

				mysqlpp::Query query = conn->query();

				std::string state_condition;
				if (org_had_legacy_ondemand_purge) {
					// look for any object that would fit legacy on-demand purge criteria.
					state_condition = "(`State`='PurgeQueued' AND `Purged`='yes') OR ";
				}

				state_condition += "`State`='Available'";

				std::string id_range;
				if (!restrictions.disable_id_range_scan) {
					std::ostringstream ostr;
					ostr << "`ID` > " << range_begin << " AND `ID` <= " << range_end << " AND ";
					id_range = ostr.str();
				}

				query << "SELECT " << retention_select_fields() << ", `State`='PurgeQueued' "
					  << "FROM `Retention_" << org_id << "` r "
					  << "WHERE " << id_range
					  << " (" << state_condition << ") " << restrictions.criteria << " "
					  << "LIMIT " << limit << " FOR UPDATE";

				MYSQL_QUERY_USE(PURGE, conn, query, res);
				while(const mysqlpp::Row row = res.fetch_row()) {
					retention_record::pointer rec = make_retention_record(row);
					bool is_legacy_purged = row[row.size() - 1];
					if(is_purgeaple(rec) || is_legacy_purged) {
						retention_set.insert(rec);
					}
				}

				const std::size_t n_found = retention_set.size();
				LogVerbose("Org: " << org_id << " Found " << n_found << " purgeables");

				// if we couldn't find find "limit" amount of items, that means we can move to the next range
				if (n_found < limit) {
					new_max_retention_id = range_end;
				} else {
					new_max_retention_id = range_begin + limit;
				}

				// scan only once if the range based scanning is disabled
				if (restrictions.disable_id_range_scan) {
					break;
				}
			}

			if (new_max_retention_id >= max_id) {
				LogVerbose("Org: "<<org_id<<" Resetting retention id to 0 for rescan");
				new_max_retention_id = 0;
			}

			if(!retention_set.empty()) {
				// Update state
				{
					mysqlpp::Query query = conn->query();
					query << "UPDATE `Retention_"<<org_id<<"` SET `State`='PurgeQueued', `Purged`='no' WHERE ID IN(";
					retention_record::set_t::const_iterator begin(retention_set.begin());
					retention_record::set_t::const_iterator end(retention_set.end());
					for(retention_record::set_t::const_iterator itr = begin; itr != end; ++itr) {
						query << (itr == begin? ' ': ',')<<(*itr)->id;
					}
					query << ')';

					MYSQL_QUERY_EXECUTE(PURGE, conn, query);
				}

				// make audit record
				coreapi_eventlog::pointer eventlog = coreapi_eventlog::create(
						api, org_id, coreapi_eventlog::levels::message, "Purge based on retention rules", "Archiving",
						"Purge:Auto");
				purge_audit<retention_record::extract_identifier>(
						conn, org_id, eventlog->id, retention_set.begin(), retention_set.end(), fn_is_filetype_auto_expired,
						eventlog->action, is_internal_filetype);

				// schedule the found purge-able items
				fn_schedule_purgeables(retention_set, eventlog->id, 0 /* not on-demand purge */);

				conn->commit_transaction();
			}

			return new_max_retention_id;
		}
		MYSQL_CATCH_LOG_ERROR(e)

		return max_retention_id;
	}

	retention_record::pointer get_retention(const archiving::archivable::identifier& identifier)
	{
		try {
			std::string uuid_hex(identifier.uuid);
			boost::algorithm::erase_all(uuid_hex, "-");

			boost::shared_ptr<rmldb::transactional_connection> conn = get_connection_wait_for(boost::chrono::seconds(5));

			mysqlpp::Query query = conn->query();
			query << "SELECT" << retention_select_fields()
				  << "FROM `Retention_%0:OrgID` WHERE `Type`=%1:FileType AND `UUID`=%2:UUID /*%3:Comment*/ LIMIT 1";
			query.parse();

			mysqlpp::SQLQueryParms qp;
			qp << identifier.org << mysqlpp::sql_int(identifier.filetype)
			   << rmldb::utils::mysqlpp_uuid_literal(identifier.uuid) << identifier.uuid;

			MYSQL_QUERY_USE(DEFAULT, conn, query, res, qp);

			return make_retention_record(res.fetch_row());
		}
		MYSQL_CATCH_LOG_ERROR(e)

		return retention_record::pointer();
	}

	types::purge_def::pointer get_orgs_for_purge()
	{
		types::purge_def::pointer ret(new types::purge_def(time(0)));

		try {
			boost::shared_ptr<rmldb::transactional_connection> conn = get_connection_wait_for(boost::chrono::seconds(5));

			const char* sql = "SELECT `OrgID` "
							  "FROM ("
							  " SELECT SUBSTRING(`TABLE_NAME` FROM 11) `OrgID`"
							  "  FROM `information_schema`.`TABLES`"
							  "  WHERE TABLES.TABLE_SCHEMA='ArchivingRetention'"
							  "   AND `TABLE_NAME` LIKE 'Retention\\_%'"
							  "   AND `TABLE_NAME` != 'Retention_Template'"
							  ") X "
							  "WHERE ( X.OrgID % 47 ) % @@auto_increment_increment + 1 = @@auto_increment_offset";

			mysqlpp::Query query = conn->query(sql);
			MYSQL_QUERY_USE(PURGE, conn, query, res);
			while (mysqlpp::Row row = res.fetch_row()) {
				types::purge_def::org_def org_def(static_cast<archiving::org_id_t>(row[0]));
				ret->set.insert(org_def);
			}
		}
		MYSQL_CATCH_LOG_ERROR(w)

		return ret;
	}

	bool usage_purge(const archiving::org_id_t& org, const archiving::file_type::file_type_t& filetype, const uint64_t& endpoint_id, const int& count, const int64_t& clear_usage, const int64_t& real_usage)
		__attribute__ ((warn_unused_result))
	{
		try {
			boost::shared_ptr<rmldb::transactional_connection> conn = get_connection_wait_for(boost::chrono::seconds(5));
			mysqlpp::Query query = conn->query();
			query << "INSERT INTO `HourlyUsage_" << org << "` (`Hour`,`Type`,`Count`,`ClearUsage`,`RealUsage`,`EndpointID`) VALUES ("
				  << " DATE_ADD(CURDATE(), INTERVAL HOUR(NOW()) HOUR), "
				  << static_cast<int>(filetype) << ", "
				  << count << ", "
				  << clear_usage << ", "
				  << real_usage << ", "
				  << endpoint_id
				  << ") "
				  << "ON DUPLICATE KEY UPDATE"
					 " `Count`=`Count`+VALUES(`Count`),"
					 " `ClearUsage`=`ClearUsage`+VALUES(`ClearUsage`),"
					 " `RealUsage`=`RealUsage`+VALUES(`RealUsage`)";
			MYSQL_QUERY_EXECUTE(PURGE, conn, query);

			return true;
		}
		MYSQL_CATCH_LOG_ERROR(e)

		return false;
	}

	uint64_t count_purge_audit_records(const archiving::archivable::identifier& identifier)
	{
		try {
			boost::shared_ptr<rmldb::transactional_connection> conn = get_connection_wait_for(boost::chrono::seconds(5));
			mysqlpp::Query query = conn->query();

			std::string uuid_hex(identifier.uuid);
			boost::algorithm::erase_all(uuid_hex, "-");
			query << "SELECT COUNT(*) FROM `Audit_"<<identifier.org<<"` "
					 "WHERE Action LIKE 'Purge:%'"
					 " AND UUID=0x"<<uuid_hex<<
					 " AND Type="<<static_cast<int>(identifier.filetype);

			MYSQL_QUERY_USE(PURGE, conn, query, res);

			return res.fetch_row().at(0);
		}
		MYSQL_CATCH_LOG_ERROR(e)

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

		try {
			boost::shared_ptr<rmldb::transactional_connection> conn = get_connection_wait_for(boost::chrono::seconds(5));

			conn->start_transaction();
			for(Iter itr = begin; itr != end; ) {
				mysqlpp::Query query = conn->query();
				query << "UPDATE ArchivingRetention.Retention_"<<org<<" SET State='Purged' WHERE ID IN (";
				for (int i = 0; i < 1000 && itr != end; i++, ++itr) {
					query << (i ? ',' : ' ')<<extract_id(*itr);
				}
				query << " )";

				MYSQL_QUERY_EXECUTE(PURGE, conn, query);
			}
			conn->commit_transaction();

			return true;
		}
		MYSQL_CATCH_LOG_ERROR(e)

		return false;
	}

	bool save(const data::multiset_t& data, const usage_data::multiset_t& usage_data)
	{
		try {
			boost::shared_ptr<rmldb::transactional_connection> conn = get_connection_wait_for(boost::chrono::seconds(5));
			conn->start_transaction();

			// retention data
			{
				const data::multiset_t::const_iterator& end(data.end());

				data::multiset_t::const_iterator batch_itr = data.begin();
				while(batch_itr != end) {
					std::pair< data::multiset_t::const_iterator, data::multiset_t::const_iterator > batch_range = data.equal_range(*batch_itr);
					const archiving::org_id_t& org(batch_itr->org);

					mysqlpp::Query query = conn->query();
					query << "INSERT INTO `Retention_" << org << "` \n"
							 "( UUID, LegUUID, Format, OrgID, UserID, PolicyID, PolicyVersion, CreationDate,"
							 " RetentionMinDate, RetentionMaxDate, MidnightOffsetSeconds, EndpointID, Type )"
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
							  <<__ws(creation_ymd)<<", "
							  <<__ws(min_retention_ymd)<<", "
							  <<__ws(max_retention_ymd)<<", "
							  <<midnight_offset_seconds<<", "
							  <<itr->endpoint_id<<", "
							  <<(int)(itr->file_type)
							  <<" )\n";
					}
					query << "ON DUPLICATE KEY UPDATE EndpointID=VALUES(EndpointID)";

					MYSQL_QUERY_EXECUTE_GET_RESULT(DEFAULT, conn, query, res);
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
					query << "INSERT INTO `HourlyUsage_"<<org<<"` \n"
							"( `Hour`, `Type`, `Count`, `ClearUsage`, `RealUsage`, `EndpointID` )"
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
							  <<static_cast<int>(itr->filetype)<<", "
							  <<"1, "
							  <<itr->clear_size<<", "
							  <<itr->real_size<<", "
							  <<itr->endpoint_id
							  <<" )\n";
					}
					query << " ON DUPLICATE KEY "
							 "UPDATE `Count`=`Count`+VALUES(`Count`), "
							 "`ClearUsage`=`ClearUsage`+VALUES(`ClearUsage`), "
							 "`RealUsage`=`RealUsage`+VALUES(`RealUsage`)";

					MYSQL_QUERY_EXECUTE(DEFAULT, conn, query);
					LogVerbose("Usage data added for Org:"<<org);

					batch_itr = batch_range.second;
				}
			}

			conn->commit_transaction();

			return true;
		}
		MYSQL_CATCH_LOG_ERROR(e)

		return false;
	}
};

}  // namespace database

}  // namespace app

#endif /* DATABASE_RETENTION_HPP_ */
