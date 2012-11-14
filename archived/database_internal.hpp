/*
 * database.hpp
 *
 *  Created on: 7 Mar 2011
 *      Author: dkorah
 */

#ifndef DATABASE_INTERNAL_HPP_
#define DATABASE_INTERNAL_HPP_

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
#include <boost/function.hpp>

#include <set>
#include <algorithm>

#include <time.h>
#include <mysql++/row.h>

namespace app {

extern std::string hostname;

namespace database {

class internal : public basic_database< internal >
{
public:
	static inline_ const char* get_identifier()
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

				SQL_DEBUG(query.str(qp));
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
						" OpState=%2:OpState, "
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

				SQL_DEBUG(query.str(qp));
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
				SQL_DEBUG(query.str(qp));

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
			policy::cached_finder_t& fn_cached_policy_finder,
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
						" WHERE HostName=%0q:HostName AND OpState!=%1:OpStateAllDone";
				query.parse();

				mysqlpp::SQLQueryParms qp;
				qp << arcd_hostname << operation_state::all_done;

				SQL_DEBUG(query.str(qp));

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
						"WHERE HostName=%0q:HostName AND OpState!=%1:OpStateAllDone "
						" AND ID <= %2:FirstMaxID AND ID > %3:MaxID "
						"LIMIT 100";
				query.parse();

				mysqlpp::SQLQueryParms qp;
				qp << arcd_hostname << operation_state::all_done << first_max_id << max_id;

				SQL_DEBUG(query.str(qp));

				mysqlpp::UseQueryResult res = query.use(qp);
				while(mysqlpp::Row row = res.fetch_row()) {

					max_id = rml::utils::strtoll(row[0].c_str());

					const archiving::archivable::file_type::file_type_t file_type(archiving::archivable::file_type::file_type_t(rml::utils::strtoull(row[5].c_str(), 0ULL)));
					const archiving::org_id_t org(rml::utils::strtoull(row[6].c_str(), 0ULL));
					const std::string uuid(row[7].c_str());
					
					// lookup the busy-set to see if we already have this loaded
					if(fn_is_already_loaded(operation_data::pointer(new operation_data(operation_data::detail::pointer(new operation_data::detail(file_type, org, uuid))))))
						continue;

					try {
						const uint64_t& item_id(rml::utils::strtoull(row[0].c_str(), 0ULL));
						const operation_state::operation_state_t saved_state(operation_state::operation_state_t(rml::utils::strtoull(row[12].c_str(), 0ULL)));

						LogDebug("Trying to load ID:"<<item_id<<' '<<file_type<<'/'<<org<<'/'<<uuid<<" state:"<<operation_state::to_string(saved_state));

						// TODO Ignoring PolicyVersion here; we need to revisit this
						const std::pair< archiving::org_id_t, policy::id_type >& lookup( std::make_pair< archiving::org_id_t, policy::id_type >(
								org, policy::id_type(rml::utils::strtoull(row[3].c_str(), 0ULL))) );
						const policy::pointer& p_policy(fn_cached_policy_finder(lookup));
						if(!p_policy) {
							fn_cached_policy_finder.forget(lookup);
							LogWarning("Failed to lookup policy:"<<lookup.first<<'/'<<lookup.second);
							continue;
						}

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
							SQL_DEBUG(query.str(qp));

							mysqlpp::UseQueryResult res = query.use(qp);
							while(mysqlpp::Row row = res.fetch_row()) {
								const std::pair<files_map_t::iterator, bool>& ret(files_map.insert(std::make_pair< types::file_format::file_format_t, rml::utils::tmp_fstream::pointer >(
										types::file_format::file_format_t(rml::utils::strtoull(row[0].c_str(), 0ULL)),
										rml::utils::tmp_fstream::create(row[1].c_str(), std::ios_base::openmode(std::fstream::in | std::fstream::out | std::fstream::binary)))));
								if(ret.second)
									ret.first->second->close();
							}
						}

						operation_data::pointer p_opdata(new operation_data());
						p_opdata->p_detail = operation_data::detail::pointer(new operation_data::detail(
								rml::utils::strtoull(row[14].c_str(), 0ULL),
								file_type, org, uuid,
								row[2].c_str(),
								p_policy, rml::utils::strtoull(row[13].c_str(), 0ULL),
								p_endpoint,
								types::file_format::file_format_t(rml::utils::strtoull(row[8].c_str(), 0ULL)),
								rml::utils::strtoull(row[15].c_str(), 0ULL), rml::utils::strtoull(row[16].c_str(), 0ULL),
								types::file_format::file_format_t(rml::utils::strtoull(row[10].c_str(), 0ULL)),
								files_map,
								saved_state,
								item_id,
								rml::utils::strtoull(row[11].c_str(), 0ULL)));

						pc_produce(channel, p_opdata);
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

			SQL_DEBUG(query.str());

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

				SQL_DEBUG(query.str(qp));

				mysqlpp::UseQueryResult res = query.use(qp);

				if(mysqlpp::Row row = res.fetch_row()) {
					path = row[1].c_str();
					id = rml::utils::strtoull(row[0].c_str());
				}
			}

			if(!path.empty() && id) {
				mysqlpp::Query query = conn->query();
				query << "UPDATE ArchivingInternal.ArchiveDClearCache SET AccessTime=NOW() WHERE ID=" << id;
				SQL_DEBUG(query.str());
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

	types::purge_def::pointer update_get_purge_def(const types::purge_def::pointer& previous, const unsigned int limit,
			const std::string& enabled_orgs)
	{
		types::purge_def::pointer p_ret(new types::purge_def());

		try {
			rml::MySQLConnection conn(*p_pool_);

			if(!previous->set.empty())
			{
				mysqlpp::Query query = conn->query();

				query << "INSERT INTO `ArchivingInternal`.`ArchiveDPurgeMarker` "
						" (OrgID, MaxRetentionID, LastRunTime, LastResetTime) "
						"VALUES ";
				const types::purge_def::set_t::const_iterator& end(previous->set.end());
				const types::purge_def::set_t::const_iterator& begin(previous->set.begin());
				for(types::purge_def::set_t::const_iterator itr = begin; itr != end; ++itr) {
					if (previous->limit || previous->offset)
						query << (itr == begin? '\n': ',') << '(' << itr->org_id << ", "<<itr->max_retention_id<<", NOW(), NOW())";
					else
						query << (itr == begin? '\n': ',') << '(' << itr->org_id << ", 0, '2009-01-01', '2009-01-01')";
				}

				if (previous->limit || previous->offset)
					query << " ON DUPLICATE KEY UPDATE LastRunTime=VALUES(LastRunTime), MaxRetentionID=VALUES(MaxRetentionID), LastResetTime=IF(0 = VALUES(MaxRetentionID), NOW(), LastResetTime)";
				else
					query << " ON DUPLICATE KEY UPDATE OrgID=VALUES(OrgID)";

				SQL_DEBUG(query.str());
				query.execute();
			}

			if(!enabled_orgs.empty()) {

				std::string where_enabled_orgs;
				{
					if(enabled_orgs != "all") {
						if(std::string::npos == enabled_orgs.find(','))
							// not a list
							where_enabled_orgs = " AND `OrgID` = "+enabled_orgs;
						else
							// a list or orgs
							where_enabled_orgs = " AND `OrgID` IN ( "+enabled_orgs+" )";
					}
				}

				p_ret->creation_time = time(0);

				// if offset is 0 and set is empty, start from begining
				const unsigned int offset((previous->offset && !previous->set.empty())? (previous->offset + previous->limit): 0);

				mysqlpp::Query query = conn->query();
				query <<"SELECT OrgID, MaxRetentionID, LastResetTime < DATE_ADD(NOW(), INTERVAL -1 DAY) LastResetMoreThanOneDayAgo, "
							"LastRunTime < DATE_ADD(NOW(), INTERVAL -1 HOUR) LastRunMoreThanOneHourAgo "
						"FROM `ArchivingInternal`.`ArchiveDPurgeMarker` "
						"WHERE (`OrgID` % @@auto_increment_increment) = (@@auto_increment_offset % @@auto_increment_increment) "<<where_enabled_orgs<<" "
						"ORDER BY LastRunTime LIMIT "<<offset<<", "<<limit;
				SQL_DEBUG(query.str());
				mysqlpp::UseQueryResult res = query.use();
				while(mysqlpp::Row row = res.fetch_row()) {
					const unsigned long long OrgID(rml::utils::strtoull(row[0].c_str()));
					const unsigned long long MaxRetentionID(rml::utils::strtoull(row[1].c_str()));
					const bool LastResetMoreThanOneDayAgo(row[2]=="1");
					const bool LastRunMoreThanOneHourAgo(row[3]=="1");
					if(LastRunMoreThanOneHourAgo) {
						if(!MaxRetentionID && !LastResetMoreThanOneDayAgo)
							continue;

						p_ret->set.insert(types::purge_def::org_def(OrgID, MaxRetentionID));
					}
				}

				p_ret->offset = offset;
				p_ret->limit = limit;
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

		return p_ret;
	}

	long get_purge_queue_size(const int& purge_max_queue_size)
	{
		try {
			rml::MySQLConnection conn(*p_pool_);
			{
				mysqlpp::Query query = conn->query();

				query << "SELECT COUNT(*) QSize, @@auto_increment_increment FROM ( "
						"    SELECT 1 FROM ArchiveDPurgeQueue "
						"        WHERE HostName=%0q:HostName "
						"            AND (`OrgID` % @@auto_increment_increment) = (@@auto_increment_offset % @@auto_increment_increment) "
						"        LIMIT %1:MinQueueSizePlusOne ) x";
				query.parse();

				mysqlpp::SQLQueryParms qp;
				qp << hostname << (purge_max_queue_size + 1);

				SQL_DEBUG(query.str(qp));
				mysqlpp::UseQueryResult res = query.use(qp);
				while(mysqlpp::Row row = res.fetch_row())
					return rml::utils::strtoull(row[0].c_str());
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

		return -1;
	}

	template< class Item >
	void batch_save(const typename Item::pointer& p_item)
	{
		typename Item::weak_ptr wp_item(p_item);

		boost::mutex::scoped_lock lock(Item::save_db_mutex);
		if(!Item::is_saved(p_item)) // already pending?!
			return;

		Item::set_saved(p_item, false);
		Item::save_db_set.insert(wp_item);

		LogDebug(p_item<<" added to save_db_set");
	}

	template< typename Item, typename ItemWeakSetType >
	void batch_save_run()
	{
		typedef ItemWeakSetType item_set;
		typedef typename ItemWeakSetType::const_iterator item_set_iterator;

		boost::mutex::scoped_lock lock(Item::save_db_mutex);

		if(Item::save_db_set.begin() == Item::save_db_set.end()) // empty?
			return;

		rml::MySQLConnection conn(*p_pool_);
		try {
			mysqlpp::Transaction txn(*conn, false);

			mysqlpp::Query query = conn->query();
			mysqlpp::SQLQueryParms qp;

			Item::save_db_begin(query, qp);

			item_set_iterator begin = Item::save_db_set.begin();
			item_set_iterator end = Item::save_db_set.end();
			for(item_set_iterator itr = begin; itr != end; ++itr) {
				if(typename Item::pointer p_item = itr->lock()) {
					query << (itr == begin? ' ': ',');
					Item::save_db(p_item, query, qp, hostname);
				}
				else
					LogDebug("Item expired; ignoring!");
			}

			Item::save_db_end(query, qp);

			if(!qp.empty()) {
				query.parse();
				SQL_DEBUG(query.str(qp));
				query.execute(qp);
			}
			else {
				SQL_DEBUG(query.str());
				query.execute();
			}

			txn.commit();

			// mark all as saved!
			for(item_set_iterator itr = Item::save_db_set.begin(); itr != Item::save_db_set.end(); ++itr) {
				typename Item::pointer p_item(itr->lock());
				if(p_item)
					Item::set_saved(p_item, true);
			}

			// empty the set!
			item_set empty_set;
			std::swap(empty_set, Item::save_db_set);
		}
		catch(const std::exception& e) {
			LogWarning(e.what());
		}
	}

	template< typename Item, typename Iter, typename ExtractID >
	bool delete_by_ids(const Iter& begin, const Iter& end, const std::string& table_name, const std::string& field_name="id")
	{
		if(begin == end)
			return true;

		// we need to lock the save actions because we are using the INSERT ... ON DUPLICATE KEY UPDATE statements
		boost::mutex::scoped_lock lock(Item::save_db_mutex);

		try {
			ExtractID extract_id;
			rml::MySQLConnection conn(*p_pool_);
			mysqlpp::Query query = conn->query();
			query << "DELETE FROM `ArchivingInternal`.`"<<table_name<<"` WHERE `"<<field_name<<"` IN (";
			for(Iter itr = begin; itr != end; ++itr)
				query << (itr == begin? ' ': ',') << extract_id(*itr);
			query << " )";

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
	
	typedef boost::function< void( mysqlpp::Query&, mysqlpp::SQLQueryParms& ) > fn_setup_query_t;
	
	template< typename Item, typename ExtractID >
	void batch_load(typename Item::list_t& loaded,
			const fn_setup_query_t& setup_select, const boost::function< typename Item::pointer( mysqlpp::Row& ) > fn_make_item_from_row,
			const fn_setup_query_t& setup_update_by_ids_begin=fn_setup_query_t(), const fn_setup_query_t& setup_update_by_ids_end=fn_setup_query_t(), 
			const unsigned int select_offset=0, const unsigned int select_limit=0)
	{
		try {
			ExtractID extract_id;
			rml::MySQLConnection conn(*p_pool_);
			mysqlpp::Transaction txn(*conn, false);
			
			mysqlpp::Query query = conn->query();
			mysqlpp::SQLQueryParms qparams;

			mysqlpp::Query update_query = conn->query();
			mysqlpp::SQLQueryParms update_qparams;
			
			setup_select(query, qparams);
			if(select_offset || select_limit)
				query << " LIMIT "<<select_offset<<", "<<select_limit;
			if(!setup_update_by_ids_begin.empty())
				query << " FOR UPDATE";
			
			mysqlpp::UseQueryResult res;
			if(!qparams.empty()) {
				query.parse();
				SQL_DEBUG(query.str(qparams));
				res = query.use(qparams);
			}
			else {
				SQL_DEBUG(query.str());
				res = query.use();
			}

			if(!setup_update_by_ids_begin.empty())
				setup_update_by_ids_begin(update_query, update_qparams);
			
			bool first = true;
			while(mysqlpp::Row row = res.fetch_row()) {
				typename Item::pointer p_item(fn_make_item_from_row(row));
				loaded.push_back(p_item);
				if(!setup_update_by_ids_begin.empty())
					update_query << (first? ' ': ',')<<extract_id(p_item);

				first = false;
			}
			
			if(!first && !setup_update_by_ids_begin.empty()) {
				if(!setup_update_by_ids_end.empty())
					setup_update_by_ids_end(update_query, update_qparams);
				else
					update_query << ')';
				
				if(!update_qparams.empty()) {
					update_query.parse();
					SQL_DEBUG(update_query.str(update_qparams));
					update_query.execute(update_qparams);
				}
				else {
					SQL_DEBUG(update_query.str());
					update_query.execute();
				}
			}
			
			txn.commit();
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
};

}  // namespace database

}  // namespace app

#endif /* DATABASE_INTERNAL_HPP_ */
