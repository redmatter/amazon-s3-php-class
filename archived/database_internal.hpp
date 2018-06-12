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
#include "macros.h"
#include "legal_hold/status.hpp"

#include <rml/database/mysql/basic_database.hpp>
#include <rml/database/mysql/errors.hpp>
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
#include <mysql++/sql_types.h>

#define distribute_org_id " ( `OrgID` % 47 ) % @@auto_increment_increment + 1 = @@auto_increment_offset "

namespace app {

extern std::string hostname;

namespace database {

namespace rmldb = rml::database::mysql;

class internal : public rmldb::basic_database<internal>
{
public:
	static inline const char* get_identifier()
	{
		return "internal";
	}

	explicit internal(const boost::program_options::variables_map& confMap) :
			rmldb::basic_database<internal>(confMap)
	{
	}

	bool save(operation_data::pointer& opdata)
	{
		try {
			boost::shared_ptr<rmldb::connection> conn = get_connection();
			mysqlpp::Transaction transaction(*conn, false);

			const operation_data::detail& opdetail(*(opdata->p_detail));

			try {
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

					SqlDebug(conn, query.str(qp));
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

					SqlDebug(conn, query.str(qp));
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
					SqlDebug(conn, query.str(qp));

					query.execute(qp);
				}

				transaction.commit();

				return true;
			}
			catch (const mysqlpp::BadQuery& e) {
				LogError("MySQL Error: " << e.what());
				if (abandon_connection_if_faulty(conn, transaction, e)) {
					return false;
				}
			}
			catch(const mysqlpp::Exception& e) {
				LogError("MySQL Error: "<<e.what());
			}
			catch(const std::exception& e) {
				LogError("Exception: "<<e.what());
			}
			catch(...) {
				LogError("Unexpected exception");
			}

			LogWarning("Rolling back transaction");
			transaction.rollback();
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
			boost::shared_ptr<rmldb::connection> conn = get_connection();
			try {
				if(first) {
					mysqlpp::Query query = conn->query();
					query << "SELECT MAX(ID) FROM ArchivingInternal.ArchiveDQueue"
							" WHERE HostName=%0q:HostName AND OpState!=%1:OpStateAllDone";
					query.parse();

					mysqlpp::SQLQueryParms qp;
					qp << arcd_hostname << operation_state::all_done;

					SqlDebug(conn, query.str(qp));

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

					SqlDebug(conn, query.str(qp));

					mysqlpp::UseQueryResult res = query.use(qp);
					while(mysqlpp::Row row = res.fetch_row()) {

						max_id = rml::utils::strtoll(row[0].c_str());

						const archiving::file_type::file_type_t file_type(archiving::file_type::file_type_t(rml::utils::strtoull(row[5].c_str(), 0ULL)));
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
								boost::shared_ptr<rmldb::connection> conn = get_connection();
								mysqlpp::Query query = conn->query();
								query << "SELECT FileFormat, Path FROM ArchivingInternal.ArchiveDQueueFiles WHERE ArchiveDQueueID=%0:ID";
								query.parse();

								mysqlpp::SQLQueryParms qp;
								qp << item_id;
								SqlDebug(conn, query.str(qp));

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
			catch (const mysqlpp::BadQuery& e) {
				LogError("MySQL Error: " << e.what());
				abandon_connection_if_faulty(conn, e);
			}
			catch(const mysqlpp::Exception& e) {
				LogError("MySQL Error: "<<e.what());
			}
			catch(const std::exception& e) {
				LogError("Exception: "<<e.what());
			}
			catch(...) {
				LogError("Unexpected exception");
			}
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
			boost::shared_ptr<rmldb::connection> conn = get_connection();
			mysqlpp::Transaction trans(*conn);

			try {
				const std::set<operation_data::pointer>::const_iterator& end(purgeables.end());
				for(std::set< operation_data::pointer >::const_iterator itr = purgeables.begin(); itr != end; ) {
					mysqlpp::Query query = conn->query();
					query << "DELETE FROM ArchivingInternal.ArchiveDQueue WHERE ID IN (";

					for (int i = 0; i < 1000 && itr != end; i++, ++itr) {
						if(!i) {
							query << ',';
						}
						query << '\n'<<(*itr)->p_detail->database_id << " /*"<<(*itr)->p_detail->identifier<<"*/";
					}
					query << ')';

					SqlDebug(conn, query.str());
					query.execute();
				}

				trans.commit();

				return true;
			}
			catch (const mysqlpp::BadQuery& e) {
				LogError("MySQL Error: "<<e.what());
				if (abandon_connection_if_faulty(conn, trans, e)) {
					return false;
				}
			}
			catch(const mysqlpp::Exception& e) {
				LogError("MySQL Error: "<<e.what());
			}
			catch(const std::exception& e) {
				LogError("Exception: "<<e.what());
			}
			catch(...) {
				LogError("Unexpected exception");
			}

			LogWarning("Rolling back transaction");
			trans.rollback();
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
			boost::shared_ptr<rmldb::connection> conn = get_connection();
			mysqlpp::Transaction trans(*conn);

			try {
				uint64_t id = 0;
				{
					mysqlpp::Query query = conn->query();
					query << "SELECT ID, Path FROM `ArchivingInternal`.`ArchiveDClearCache` "
							 "WHERE OrgID=%0:OrgID AND UUID=UNHEX(REPLACE(%1q:Identifier, '-', ''))"
							 " AND FileFormat=%2:FileFormat"
							 " AND HostName=%3q:HostName "
							 "FOR UPDATE";
					query.parse();

					mysqlpp::SQLQueryParms qp;
					qp << org_id << uuid << file_format << app::hostname;

					SqlDebug(conn, query.str(qp));

					mysqlpp::UseQueryResult res = query.use(qp);

					if(mysqlpp::Row row = res.fetch_row()) {
						path = row[1].c_str();
						id = rml::utils::strtoull(row[0].c_str());
					}
				}

				if(path.empty() || !id) {
					throw rml::exception("No path found in clear cache");
				}

				mysqlpp::Query query = conn->query();
				query << "UPDATE ArchivingInternal.ArchiveDClearCache SET AccessTime=NOW() WHERE ID=" << id;
				SqlDebug(conn, query.str());
				query.execute();

				trans.commit();

				return path;
			}
			catch (const mysqlpp::BadQuery& e) {
				if(e.errnum() == ER_LOCK_DEADLOCK || e.errnum() == ER_LOCK_WAIT_TIMEOUT) {
					LogWarning("Ignoring UPDATE failure: "<<e.what());
					return path;
				}

				LogError("Bad Query: "<<e.what());
				abandon_connection_if_faulty(conn, trans, e);

				throw;
			}
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
			boost::shared_ptr<rmldb::connection> conn = get_connection();
			try {
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

				SqlDebug(conn, query.str(qp));
				query.execute(qp);

				return true;

			}
			catch (const mysqlpp::BadQuery& e) {
				LogError("MySQL Error: "<<e.what());
				if (abandon_connection_if_faulty(conn, e)) {
					return false;
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

private:
	static inline std::string get_org_list_where_condition(const std::string& orgs_list)
	{
		if (orgs_list == "all") {
			return std::string();
		}

		if(std::count(orgs_list.begin(), orgs_list.end(), ',') == 0) {
			// if it is not a list use 'FieldName=OrgId' form
			return boost::str(boost::format(" AND `OrgID` = %1% ") % orgs_list);
		}

		// else 'FieldName IN (OrgIdList)' form
		return boost::str(boost::format(" AND `OrgID` IN ( %1% ) ") % orgs_list);
	}

public:
	types::purge_def::pointer update_get_purge_def(const types::purge_def::pointer& previous, const unsigned int limit,
			const std::string& enabled_orgs)
	{
		try {
			types::purge_def::pointer p_ret(new types::purge_def());

			boost::shared_ptr<rmldb::connection> conn = get_connection();

			try {
				if(!previous->set.empty())
				{
					const bool not_the_very_first_time(previous->limit || previous->offset);

					mysqlpp::Query query = conn->query();

					query << "INSERT INTO `ArchivingInternal`.`ArchiveDPurgeMarker` "
							 " (OrgID, MaxRetentionID, LastRunTime, LastResetTime) "
							 "VALUES ";
					const types::purge_def::set_t::const_iterator& end(previous->set.end());
					const types::purge_def::set_t::const_iterator& begin(previous->set.begin());
					for(types::purge_def::set_t::const_iterator itr = begin; itr != end; ++itr) {
						if (not_the_very_first_time)
							query << (itr == begin? '\n': ',') << '(' << itr->org_id << ", "<<itr->max_retention_id<<", NOW(), NOW())";
						else
							query << (itr == begin? '\n': ',') << '(' << itr->org_id << ", 0, '2009-01-01', '2009-01-01')";
					}

					if (not_the_very_first_time) {
						query << " ON DUPLICATE KEY UPDATE"
								 " LastRunTime=VALUES(LastRunTime),"
								 " MaxRetentionID=VALUES(MaxRetentionID),"
								 " LastResetTime=IF(0 = VALUES(MaxRetentionID), NOW(), LastResetTime)";
					}
					else
						query << " ON DUPLICATE KEY UPDATE OrgID=VALUES(OrgID)";

					PURGE_SQL_DEBUG(conn, query.str());
					query.execute();
				}

				if(!enabled_orgs.empty()) {
					const bool very_few_orgs_enabled(std::count(enabled_orgs.begin(), enabled_orgs.end(), ',') < 15);

					p_ret->creation_time = time(0);

					// if offset is 0 and set is empty, start from begining
					const unsigned int offset((previous->offset && !previous->set.empty())? (previous->offset + previous->limit): 0);

					mysqlpp::Query query = conn->query();
					query <<"SELECT OrgID, MaxRetentionID, LastResetTime < DATE_ADD(NOW(), INTERVAL -1 DAY) LastResetMoreThanOneDayAgo, "
							"LastRunTime < DATE_SUB(NOW(), INTERVAL "<<(very_few_orgs_enabled? 20: 60)<<" MINUTE) LastRunMoreThan__MinutesAgo "
																										"FROM `ArchivingInternal`.`ArchiveDPurgeMarker` "
																										"WHERE "distribute_org_id<<get_org_list_where_condition(enabled_orgs)<<
						  "ORDER BY LastRunTime LIMIT "<<offset<<", "<<limit;
					PURGE_SQL_DEBUG(conn, query.str());
					mysqlpp::UseQueryResult res = query.use();
					while(mysqlpp::Row row = res.fetch_row()) {
						const unsigned long long OrgID(rml::utils::strtoull(row[0].c_str()));
						const unsigned long long MaxRetentionID(rml::utils::strtoull(row[1].c_str()));
						/*
						const bool LastResetMoreThanOneDayAgo(row[2]=="1");
						*/
						const bool LastRunMoreThan__MinutesAgo(row[3]=="1");
						// If we loaded more than XX minutes ago, we need to load further for the Org
						// only if it has not already been reset to zero within the past one day
						//
						// Update: Removing the LastResetMoreThanOneDayAgo check; the LastRunTime is always updated
						// so in theory it is enough only to check the interval between last attempt and this.
						if(LastRunMoreThan__MinutesAgo) {
							/*
							if(!MaxRetentionID && !LastResetMoreThanOneDayAgo)
								continue;
							*/

							p_ret->set.insert(types::purge_def::org_def(OrgID, MaxRetentionID));
						}
					}

					p_ret->offset = offset;
					p_ret->limit = limit;

					return p_ret;
				}
			}
			catch (const mysqlpp::BadQuery& e) {
				LogError("MySQL Error: "<<e.what());
				abandon_connection_if_faulty(conn, e);
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

		return types::purge_def::pointer();
	}

	long get_purge_queue_size(const int& purge_max_queue_size, const std::string& enabled_orgs, const std::string& state_filter)
	{
		try {
			boost::shared_ptr<rmldb::connection> conn = get_connection();
			try {
				mysqlpp::Query query = conn->query();

				query << "SELECT COUNT(*) QSize, @@auto_increment_increment FROM ( "
						"    SELECT 1 FROM ArchiveDPurgeQueue "
						"        WHERE HostName=%0q:HostName"
						"            AND "distribute_org_id
						<<           state_filter
						<<           get_org_list_where_condition(enabled_orgs)<<
						"        LIMIT %1:MinQueueSizePlusOne ) x";
				query.parse();

				mysqlpp::SQLQueryParms qp;
				qp << hostname << (purge_max_queue_size + 1);

				PURGE_SQL_DEBUG(conn, query.str(qp));
				mysqlpp::UseQueryResult res = query.use(qp);
				while(mysqlpp::Row row = res.fetch_row()) {
					return rml::utils::strtoull(row[0].c_str());
				}
			}
			catch (const mysqlpp::BadQuery& e) {
				LogError("MySQL Error: "<<e.what());
				abandon_connection_if_faulty(conn, e);
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
		if(Item::is_save_pending(p_item)) // already pending?!
			return;

		boost::mutex::scoped_lock lock(Item::save_db_mutex);

		Item::set_save_pending(p_item, true);
		Item::save_db_set.insert(typename Item::weak_ptr(p_item));

		LogDebug(p_item<<" added to save_db_set");
	}

	template< typename Item, typename ItemWeakSetType >
	void batch_save_run(const boost::posix_time::ptime& run_till) try
	{
		typedef ItemWeakSetType item_set;
		typedef typename ItemWeakSetType::const_iterator item_set_iterator;

		boost::mutex::scoped_lock lock(Item::save_db_mutex);

		if(Item::save_db_set.begin() == Item::save_db_set.end()) // empty?
			return;

		boost::shared_ptr<rmldb::connection> conn = get_connection();
		mysqlpp::Transaction txn(*conn, false);

		try {
			item_set_iterator begin = Item::save_db_set.begin();
			item_set_iterator end = Item::save_db_set.end();
			item_set_iterator processed_end;
			for (item_set_iterator itr = begin; itr != end;) {
				if (boost::posix_time::microsec_clock::universal_time() > run_till) {
					LogDWarning("Allowed time has run out; giving up", "IGNO");
					break;
				}

				item_set_iterator batch_begin = itr;
				item_set_iterator batch_end = end;
				if (std::distance(batch_begin, batch_end) > 1000) {
					batch_end = batch_begin;
					std::advance(batch_end, 1000);
				}

				mysqlpp::Query query = conn->query();
				mysqlpp::SQLQueryParms qp;

				Item::save_db_begin(query, qp);

				bool first(true);
				bool any_found(false);

				for (; itr != batch_end; ++itr) {
					if(typename Item::pointer p_item = itr->lock()) {

						// in case we hit finished before we got to saving it
						if(!Item::is_save_pending(p_item))
							continue;

						query << (first? ' ': ','); first = false;
						Item::save_db(p_item, query, qp, hostname);
						any_found = true;
					}
					else
						LogDebug("Item expired; ignoring!");
				}

				if(any_found) {
					Item::save_db_end(query, qp);

					if(!qp.empty()) {
						query.parse();
						SqlDebug(conn, query.str(qp));
						query.execute(qp);
					}
					else {
						SqlDebug(conn, query.str());
						query.execute();
					}
				}

				processed_end = batch_end;
			}

			txn.commit();

			// mark all as saved!
			for(item_set_iterator itr = begin; itr != processed_end; ++itr) {
				typename Item::pointer p_item(itr->lock());
				if(p_item)
					Item::set_save_pending(p_item, false);
			}

			if (processed_end == end) {
				// empty the set!
				item_set empty_set;
				std::swap(empty_set, Item::save_db_set);
			} else {
				// otherwise erase those we are done with
				Item::save_db_set.erase(begin, processed_end);
			}
		}
		catch (const mysqlpp::BadQuery& e) {
			LogError("MySQL Error: "<<e.what());
			abandon_connection_if_faulty(conn, txn, e);
			throw;
		}
	}
	catch(const std::exception& e) {
		LogWarning("Exception: "<<e.what());
	}
	catch(...) {
		LogWarning("Exception: Unknown or Unexpected");
	}

	template< typename Item, typename Iter, typename ExtractID, typename ExtractIdentifier >
	bool delete_by_ids(const Iter& begin, const Iter& end, const std::string& table_name, const std::string& field_name="id")
	{
		if(begin == end)
			return true;

		// we need to lock the save actions because we are using the INSERT ... ON DUPLICATE KEY UPDATE statements
		boost::mutex::scoped_lock lock(Item::save_db_mutex);

		try {
			static ExtractID extract_id;
			static ExtractIdentifier extract_identifier;

			boost::shared_ptr<rmldb::connection> conn = get_connection();
			mysqlpp::Transaction txn(*conn, false);

			try {
				std::list<uint64_t> ids_list;

				for(Iter itr = begin; itr != end; ++itr) {
					uint64_t id = extract_id(*itr);

					if(id) {
						ids_list.push_back(id);
					}
					else {
						const archiving::archivable::identifier& identifier(
								extract_identifier(*itr));

						// we do not have the id updated in the object after save()
						// here we need to delete by the identifier triplet!
						// this is more likely to happen if automatic purge is in progress.
						// this is not so bad as we are within a transaction (thats the theory at least)
						// todo(dino.korah) how to reload the ID after save
						mysqlpp::Query query = conn->query();

						std::string uuid_hex(identifier.uuid);
						boost::algorithm::erase_all(uuid_hex, "-");

						query << "DELETE FROM `ArchivingInternal`.`" << table_name << "`"
																					  " WHERE `OrgID` = " << identifier.org << " AND `FileType` = "<< (int)(identifier.filetype) <<" AND `UUID`=0x"<<uuid_hex<<" /* "<< identifier.uuid <<" */";
						SqlDebug(conn, query.str());
						query.execute();
					}
				}

				if (!ids_list.empty()) {
					std::list<uint64_t>::iterator ids_begin = ids_list.begin();
					std::list<uint64_t>::iterator ids_end = ids_list.end();

					std::list<uint64_t>::iterator ids_itr = ids_begin;
					while (ids_itr != ids_end) {
						bool batch_first = true;

						mysqlpp::Query ids_query = conn->query();
						ids_query << "DELETE FROM `ArchivingInternal`.`"<<table_name<<"` WHERE `"<<field_name<<"` IN (";

						for (int i = 0; i < 1000 && ids_itr != ids_end; i++, ++ids_itr) {
							ids_query << (batch_first? ' ': ',') << *ids_itr;
							batch_first = false;
						}

						ids_query << " )";

						SqlDebug(conn, ids_query.str());
						ids_query.execute();
					}
				}

				txn.commit();

				return true;
			}
			catch (const mysqlpp::BadQuery& e) {
				LogError("MySQL Error: "<<e.what());
				abandon_connection_if_faulty(conn, txn, e);
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

	/**
	 * Get the max-id in the purge queue when we restart, so that we can later load up to that.
	 *
	 * @return
	 */
	inline uint64_t get_purge_queue_max_id() try
	{
		boost::shared_ptr<rmldb::connection> conn = get_connection();
		try {
			mysqlpp::Query query = conn->query();

			query << "SELECT MAX(ID) FROM `ArchivingInternal`.`ArchiveDPurgeQueue`";
			PURGE_SQL_DEBUG(conn, query.str());

			mysqlpp::UseQueryResult res = query.use();
			if(mysqlpp::Row row = res.fetch_row()) {
				return rml::utils::strtoull(row[0].c_str(), 0ULL);
			}
		}
		catch (const mysqlpp::BadQuery& e) {
			LogError("MySQL Error: "<<e.what());
			abandon_connection_if_faulty(conn, e);
		}

		return 0;
	}
	catch(const mysqlpp::Exception& e) {
		LogError("MySQL Exception: "<<e.what());
		return 0;
	}
	catch(const std::exception& e) {
		LogError("Exception: "<<e.what());
		return 0;
	}
	catch(...) {
		LogError("Unknown Exception");
		return 0;
	}

	typedef boost::function< void( mysqlpp::Query&, mysqlpp::SQLQueryParms& ) > fn_setup_query_t;
	
	template< typename Item, typename ExtractID >
	void batch_load(typename Item::list_t& loaded,
			const fn_setup_query_t& fn_setup_select,
			const boost::function< typename Item::pointer( mysqlpp::Row& ) > fn_make_item_from_row,
			const fn_setup_query_t& fn_setup_update_by_ids_begin=fn_setup_query_t(), const fn_setup_query_t& fn_setup_update_by_ids_end=fn_setup_query_t(),
			const unsigned int select_limit=0, const unsigned int select_offset=0)
	{
		try {
			ExtractID extract_id;

			boost::shared_ptr<rmldb::connection> conn = get_connection();
			mysqlpp::Transaction txn(*conn, false);

			try {

				mysqlpp::Query query = conn->query();
				mysqlpp::SQLQueryParms qparams;

				mysqlpp::Query update_query = conn->query();
				mysqlpp::SQLQueryParms update_qparams;

				fn_setup_select(query, qparams);
				if(select_offset || select_limit) {
					if(select_offset && select_limit)
						query << " LIMIT "<<select_offset<<", "<<select_limit;

					else if(select_limit)
						query << " LIMIT "<<select_limit;

						// else we have limit=0 with offset!=0
					else {
						LogMessage("None to SELECT as LIMIT is 0");
						return;
					}
				}

				if(!fn_setup_update_by_ids_begin.empty())
					query << " FOR UPDATE";

				mysqlpp::UseQueryResult res;
				if(!qparams.empty()) {
					query.parse();
					PURGE_SQL_DEBUG(conn, query.str(qparams));
					res = query.use(qparams);
				}
				else {
					PURGE_SQL_DEBUG(conn, query.str());
					res = query.use();
				}

				if(!fn_setup_update_by_ids_begin.empty())
					fn_setup_update_by_ids_begin(update_query, update_qparams);

				bool first = true;
				while(mysqlpp::Row row = res.fetch_row()) {
					typename Item::pointer p_item(fn_make_item_from_row(row));
					loaded.push_back(p_item);
					if(!fn_setup_update_by_ids_begin.empty())
						update_query << (first? ' ': ',')<<extract_id(p_item);

					first = false;
				}

				if(!first && !fn_setup_update_by_ids_begin.empty()) {
					if(!fn_setup_update_by_ids_end.empty())
						fn_setup_update_by_ids_end(update_query, update_qparams);

					if(!update_qparams.empty()) {
						update_query.parse();
						PURGE_SQL_DEBUG(conn, update_query.str(update_qparams));
						update_query.execute(update_qparams);
					}
					else {
						PURGE_SQL_DEBUG(conn, update_query.str());
						update_query.execute();
					}
				}

				txn.commit();
			}
			catch (const mysqlpp::BadQuery& e) {
				LogError("MySQL Error: "<<e.what());
				abandon_connection_if_faulty(conn, txn, e);
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
	}

	inline legal_hold_status get_legal_hold_status(const archiving::org_id_t org_id, const uint64_t retention_id)
	{
		try
		{
			boost::shared_ptr<rmldb::connection> conn = get_connection();
			try {
				mysqlpp::Query query = conn->query();

				query << "SELECT `LegalHoldCount`, `RestrictAccessCount` "
						 "FROM `ArchivingInternal`.`LegalHoldCache` "
						 "WHERE `OrgID` = %1:OrgID AND `RetentionID` = %2:RetentionID "
						 "LIMIT 1";
				query.parse();

				mysqlpp::SQLQueryParms qp;
				qp << org_id << retention_id;

				SqlDebug(conn, query.str(qp));

				mysqlpp::StoreQueryResult res = query.store();
				if (!res.empty()) {
					const mysqlpp::Row& row(res[0]);
					return legal_hold_status(static_cast<int>(row[0]) > 0, static_cast<int>(row[1]) > 0);
				}
			}
			catch (const mysqlpp::BadQuery& e) {
				abandon_connection_if_faulty(conn, e);
				throw;
			}
		}
		catch(const std::exception& e) {
			LogError("Exception: "<<e.what());
		}

		return legal_hold_status();
	}
};

}  // namespace database

}  // namespace app

#endif /* DATABASE_INTERNAL_HPP_ */
