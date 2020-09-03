//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 07 March 2011.
//

#ifndef DATABASE_HPP_
#define DATABASE_HPP_

#include "../common/policy.hpp"

#include <rml/database/mysql/basic_database.hpp>
#include <rml/database/mysql/transactional_connection.hpp>
#include <rml/database/mysql/utils.hpp>
#include <rml/exception.h>
#include <rml/logger.h>

#include <mysql++/sql_types.h>

#include <boost/program_options.hpp>
#include <boost/thread/thread.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#include <set>

#include <time.h>

namespace app {

extern std::string hostname;

namespace database {

namespace mysql = rml::database::mysql;

class internal : public mysql::basic_database<internal, mysql::transactional_connection>
{
	typedef mysql::basic_database<internal, mysql::transactional_connection> basic_database_type;
	typedef boost::shared_ptr<mysql::transactional_connection> connection_type;

protected:
	std::string table_name_;

public:
	static inline const char* get_identifier()
	{
		return "internal";
	}

	internal(const boost::program_options::variables_map& config, const char* table_name)
	  : basic_database_type(config)
	  , table_name_(table_name)
	{}

	/**
	 * Save archivable
	 *
	 * @param p_arc
	 */
	bool save(archiving::archivable::pointer& p_arc)
	try {
		connection_type conn = pool_->grab();
		archiving::archivable& arc(*p_arc);

		std::pair< std::string, std::string > paths = arc.get_paths();
		mysqlpp::Null<mysqlpp::sql_character_varying> modified_path( mysqlpp::null );
		if(!paths.second.empty()) {
			modified_path = paths.second;
		}

		std::string userIDs_str;
		{
			std::ostringstream ostr;
			const std::vector< archiving::user_id_t >& userIDs( arc.get_owners() );
			std::vector< archiving::user_id_t >::const_iterator itr = userIDs.begin();
			for(; itr != userIDs.end(); ++itr) {
				if(itr != userIDs.begin()) {
					ostr << ',';
				}

				ostr << *itr;
			}

			userIDs_str = ostr.str();
		}

		std::string md5_str;
		{
			std::ostringstream ostr;
			ostr << arc.get_md5();
			md5_str = ostr.str();
		}

		mysqlpp::Query query = conn->query();

		if(!arc.get_database_id()) {
			LogVerbose("Insert: "<<arc);
			query << "INSERT INTO `ArchivingInternal`.`" << table_name_ << "`";
		} else {
			LogVerbose("Update "<<arc);
			query << "UPDATE `ArchivingInternal`.`" << table_name_ << "`";
		}

		query << " SET"

				" ParentID=%0:ParentID,"
				" FileType=%1:FileType,"

				" OrgID=%2:OrgID,"
				" UserIDs=%3q:UserIDs,"

				" UUID=%4:UUID,"
				" OriginatorUUID=%5:OriginatorUUID,"

				" Path=%6q:Path,"
				" ModifiedPath=%7q:ModifiedPath,"

				" State=%8q:State,"

				" PolicyID=%9:PolicyID,"
				" MD5=UNHEX(%10q:MD5),"
				" AddToCDRMunchCache=%11q:AddToCDRMunchCache,"
				" BufferedRecording=%12q:BufferedRecording,"
				" DataFormat=%13q:DataFormat,"

				" Retry=%14:Retry,"

				" StartEpoch=%15:StartEpoch,"
				" HostName=%16q:HostName,"

				" UpdateTime=NOW(),"
				" LegUUID=%17:LegUUID"
				;

		if(arc.get_database_id()) {
			query << " WHERE ID=%18:ID";
		}

		query.parse();

		using mysql::utils::mysqlpp_uuid_literal;
		using mysql::utils::mysqlpp_uuid_literal_or_null;

		mysqlpp::SQLQueryParms qp;
		qp		<< mysqlpp::sql_bigint_unsigned(0ULL) // remove this once ParentID is removed fully
				<< mysqlpp::sql_int(arc.get_type())

				<< mysqlpp::sql_int(arc.get_org_id())
				<< mysqlpp::sql_character_varying(userIDs_str)

				<< mysqlpp_uuid_literal(arc.get_uuid())
				<< mysqlpp_uuid_literal(arc.get_originator_uuid())

				<< mysqlpp::sql_character_varying(paths.first)
				<< modified_path

				<< mysqlpp::sql_enum(archiving::archivable::state::to_string(arc.get_current_state()))

				<< mysqlpp::sql_int(arc.get_policy_id())
				<< mysqlpp::sql_char(md5_str)
				<< mysqlpp::sql_enum(arc.get_add_to_cdrmunch_cache()? "true": "false")
				<< mysqlpp::sql_enum("false")
				<< mysqlpp::sql_char(arc.get_data_format())

				<< mysqlpp::sql_int(arc.get_attempt())

				<< mysqlpp::sql_int(arc.get_start_epoch())
				<< mysqlpp::sql_char(arc.get_hostname())

				<< mysqlpp_uuid_literal_or_null(arc.get_leg_uuid())
				;

		if(arc.get_database_id()) {
			qp << arc.get_database_id();
		}

		std::cout << "SQL: "<<query.str(qp) << std::endl;
		query.execute(qp);

		if(!arc.get_database_id()) {
			LogVerbose(arc<<" ID: "<<query.insert_id());
			arc.set_database_id(query.insert_id());
		}

		return true;
	} catch(const std::exception& e) {
		LogWarning("Exception: "<<e.what());
		return false;
	}

	bool is_queued(archiving::archivable::pointer& p_arc)
	try {
		using mysql::utils::mysqlpp_uuid_literal;

		archiving::archivable& arc(*p_arc);

		connection_type conn = pool_->grab();

		mysqlpp::Query query = conn->query();
		query << "SELECT COUNT(*) FROM `ArchivingInternal`.`" << table_name_ << "` WHERE FileType=%0:FileType AND UUID=%1:UUID AND HostName=%2q:HostName";
		query.parse();

		mysqlpp::SQLQueryParms qp;
		qp << mysqlpp::sql_int(p_arc->get_type()) << mysqlpp_uuid_literal(arc.get_uuid()) << app::hostname;

		std::cout << "SQL: "<<query.str(qp) << std::endl;

		mysqlpp::UseQueryResult res = query.use(qp);
		if (mysqlpp::Row row = res.fetch_row()) {
			unsigned long count = row[0];
			return count != 0;
		}

		return false;
	} catch(const std::exception& e) {
		LogWarning("Exception: "<<e.what());
		// it is odd to return true here, but we dont want to load any file if DB is out of action
		return true;
	}

	/**
	 * Find archivables from DB
	 * ( reloading after restart )
	 *
	 * @param pbx_hostname
	 * @param fn_get_loaded_instance
	 * @param in_channel
	 * @param have_more_to_load
	 */
	void find_archivables(
			const std::string& pbx_hostname,
			const boost::function<archiving::archivable::pointer(const archiving::archivable::pointer&)>& fn_get_loaded_instance,
			archiving::archivable_channel_t& in_channel,
			bool& have_more_to_load,
			const float& loading_factor)
	try {
		using mysql::utils::format_uuid_field;

		static bool first = true;
		static uint64_t first_max_id = 0;
		static uint64_t max_id = 0;

		const int loading_limit(static_cast<int>(500.0f * std::min(1.0f, loading_factor)));
		if (loading_limit <= 0) {
			LogMessage("Skipping loading from database due to loading-factor: "<<loading_factor);
			return;
		}

		connection_type conn = pool_->grab();

		if(first) {
			mysqlpp::Query query = conn->query();
			query << "SELECT MAX(ID) FROM ArchivingInternal." << table_name_ << " WHERE HostName=%0q:HostName AND Retry < 5 AND State NOT IN ('stored','discarded')";
			query.parse();

			mysqlpp::SQLQueryParms qp;
			qp << pbx_hostname;

			std::cout << "SQL: "<<query.str(qp) << std::endl;

			mysqlpp::UseQueryResult res = query.use(qp);
			while(mysqlpp::Row row = res.fetch_row())
				if(row[0] != "NULL")
					first_max_id = rml::utils::strtoull(row[0].c_str(), 0UL);

			first = false;
		}

		if(first_max_id && max_id <= first_max_id) {
			mysqlpp::Query query = conn->query();
			query << "SELECT"
					 " c.ID, c.FileType, "<<format_uuid_field("c.UUID", "c_UUID")<<" "
					 "FROM"
					 " ArchivingInternal." << table_name_ << " c "
					 "WHERE"
					 " c.HostName=%0q:HostName AND c.Retry < 5 AND c.State NOT IN ('stored','discarded')"
					 " AND c.ID <= %1:FirstMaxID AND c.ID > %2:MaxID "
					 "ORDER BY c.ID ASC "
					 "LIMIT "<<loading_limit;
			query.parse();

			mysqlpp::SQLQueryParms qp;
			qp << pbx_hostname << first_max_id << max_id;

			std::cout << "SQL: "<<query.str(qp) << std::endl;

			mysqlpp::UseQueryResult res = query.use(qp);
			while(mysqlpp::Row row = res.fetch_row()) {
				const uint64_t& archivable_id(rml::utils::strtoull(row[0].c_str()));
				max_id = archivable_id;

				const archiving::file_type::file_type_t archivable_file_type = archiving::file_type::file_type_t(row[1].conv(0));
				const std::string archivable_uuid(row[2].c_str());

				try{
					archiving::archivable::pointer p_archivable = fn_get_loaded_instance(archiving::archivable::pointer(
							new archiving::archivable(archivable_uuid, archivable_file_type)));

					if(p_archivable) {
						LogDebug("Already loaded; Skipping; ID:"<<archivable_id<<"  "<<archivable_file_type<<"/--/"<<archivable_uuid);
						continue;
					}

					LogVerbose("Loading from database; ID:"<<archivable_id<<"  "<<archivable_file_type<<"/--/"<<archivable_uuid);
					p_archivable =  load_archivable_by_id(archivable_id);
					pc_produce(in_channel, p_archivable, p_archivable->get_attempt() * 3);
				}
				catch(const std::exception& e) {
					LogWarning("Exception: "<<e.what());
				}
			}
		}

		have_more_to_load = (first_max_id && (max_id < first_max_id)); // more to load?
	} catch(const std::exception& e) {
		LogWarning("Exception: "<<e.what());
	}

	/**
	 * Load archivable by a key/value pair
	 *
	 * @param key
	 * @param value
	 * @return
	 */
	template <typename ValueType>
	archiving::archivable::pointer load_archivable_by_key_value(const std::string &key, const ValueType &value)
	{
		using mysql::utils::format_uuid_field;
		try {
			connection_type conn = pool_->grab();

			mysqlpp::Query query = conn->query();
			query << "SELECT"
					 " ParentID,"
					 " FileType,"
					 " OrgID,"
					<< format_uuid_field("UUID", "str_UUID") << ","
					 " Path,"
					 " ModifiedPath,"
					 " State,"
					 " PolicyID,"
					 " HEX(MD5),"
					 " AddToCDRMunchCache,"
					 " DataFormat,"
					 " Retry,"
					 " StartEpoch,"
					 " HostName,"
					<< format_uuid_field("OriginatorUUID", "OrigUUID") << ","
					 " UserIDs,"
					 " ID,"
					 " BufferedRecording, "
					<< format_uuid_field("LegUUID", "LegUUID") << " "
					 "FROM ArchivingInternal." << table_name_ << " "
					 "WHERE %0:key = %1:value";
			query.parse();

			std::cout << "SQL: " << query.str(key, value) << std::endl;

			mysqlpp::UseQueryResult res = query.use(key, value);
			if (mysqlpp::Row row = res.fetch_row()) {
				archiving::file_type::file_type_t type =
				  archiving::file_type::file_type_t(rml::utils::strtoull(row[1].c_str(), 0ULL));
				archiving::archivable::pointer p_arc(
				  new archiving::archivable(row[3].c_str(),
											(row[14] != "NULL") ? row[14].c_str() : "",
											rml::utils::strtoull(row[12].c_str(), 0ULL),
											row[4].c_str(),
											type,
											row[10].c_str(),
											row[13].c_str(),
											app::policy::id_type(rml::utils::strtoull(row[7].c_str(), 0ULL)),
											row[9] == "true",
											row[5] != "NULL",
											(row[5] != "NULL") ? row[5].c_str() : "",
											row[8].c_str(),
											archiving::archivable::state::from_string(row[6].c_str())
											// , row[17] == "true" // BufferedRecording
											));

				const uint64_t& id = rml::utils::strtoull(row[16].c_str(), 0ULL);
				p_arc->set_database_id(id);
				archiving::owner_info owner((archiving::org_id_t)rml::utils::strtoull(row[2].c_str(), 0ULL));
				if (!row[15].is_null()) {
					const std::string userIds(row[15].c_str());
					std::istringstream str(userIds);
					std::string userId_str;
					while (std::getline(str, userId_str, ','))
						owner.addUser((archiving::user_id_t)rml::utils::strtoull(userId_str.c_str(), 0ULL));
					p_arc->set_owners(owner);
				}

				if (!row[18].is_null()) {
					p_arc->set_leg_uuid(row[18].c_str());
				}

				return p_arc;
			}
		} catch (const std::exception& e) {
			LogWarning("Exception: " << e.what());
		}

		THROW_RML_EXCEPTION("Archivable object not found: " << key << " == " << value);
	}

	/**
	 * Load archivable by its database id
	 *
	 * @param id
	 * @return
	 */
	archiving::archivable::pointer load_archivable_by_id(const uint64_t& id)
	{
		return load_archivable_by_key_value ("ID", mysqlpp::sql_bigint(id));
	}

	/**
	 * Load archivable by its uuid
	 *
	 * @param uuid
	 * @return
	 */
	archiving::archivable::pointer load_archivable_by_uuid(const std::string &uuid)
	{
		using mysql::utils::mysqlpp_uuid_literal;
		return load_archivable_by_key_value("UUID", mysqlpp_uuid_literal(uuid));
	}

	struct purgedef
	{
		typedef boost::shared_ptr< purgedef > pointer;
		typedef std::vector< pointer > pointer_vector;

		const uint64_t id;
		const archiving::file_type::file_type_t file_type;
		const std::string uuid;
		const std::string path;
		const std::string modified_path;

		purgedef(
				const uint64_t& id,
				const archiving::file_type::file_type_t& file_type,
				const std::string& uuid,
				const std::string& path,
				const std::string& modified_path) :
					id(id), file_type(file_type), uuid(uuid), path(path), modified_path(modified_path)
		{
		}
	};

	/**
	 * Purge saved archivables from DB
	 *
	 * @param done_items_max_age_minutes
	 * @param max_age_days
	 */
	purgedef::pointer_vector purge_find(const std::string& pbx_hostname, const unsigned int done_items_max_age_minutes, const unsigned int limit)
	{
		using mysql::utils::format_uuid_field;

		purgedef::pointer_vector ret;

		try {
			connection_type conn = pool_->grab();
			mysqlpp::Query query = conn->query();
			query << "SELECT"
					 " ID, FileType, " << format_uuid_field("UUID", "UUID") << ", Path, ModifiedPath "
					 "FROM ArchivingInternal." << table_name_ << " "
					 "WHERE HostName=%0q:HostName"
					 " AND State IN ('stored','discarded')"
					 " AND DATE_SUB(NOW(), INTERVAL %1:DoneMaxAge MINUTE) > UpdateTime";
			if(limit)
				query << " LIMIT "<<limit;

			query.parse();

			mysqlpp::SQLQueryParms qp;
			qp << pbx_hostname << done_items_max_age_minutes;

			std::cout << "SQL: "<<query.str(qp) << std::endl;

			mysqlpp::UseQueryResult res = query.use(qp);
			while(mysqlpp::Row row = res.fetch_row()) {
				ret.push_back(purgedef::pointer(new purgedef(
						rml::utils::strtoull(row[0].c_str(), 0ULL), archiving::file_type::file_type_t(rml::utils::strtoull(row[1].c_str(), 0ULL)), row[2].c_str(), row[3].c_str(), (row[4]=="NULL"? "": row[4].c_str()))));
			}
		} catch(const std::exception& e) {
			LogWarning("Exception: "<<e.what());
		}

		return ret;
	}

	/**
	 * Purge records
	 *
	 * @param purgeables
	 * @return
	 */
	bool purge(const purgedef::pointer_vector& purgeables)
	{
		if(purgeables.empty()) {
			return true;
		}

		try {
			int xx = 1;
			connection_type conn = pool_->grab();
			mysqlpp::Query query = conn->query();
			query << "DELETE FROM ArchivingInternal." << table_name_ << " WHERE ID IN (";
			for(purgedef::pointer_vector::const_iterator itr = purgeables.begin();
					itr != purgeables.end(); ++itr) {
				if(itr != purgeables.begin())
					query << ", ";

				query << (*itr)->id << "/* "<<archiving::file_type::to_string((*itr)->file_type)<<'/'<<(*itr)->uuid<<" */"<<((0 == (xx++ % 4))? '\n': ' ');
			}

			query << ')';

			std::cout << "SQL: "<<query.str() << std::endl;

			mysqlpp::SimpleResult res = query.execute();
			LogMessage(res.rows()<<" rows deleted.");

			return true;
		} catch(const std::exception& e) {
			LogWarning("Exception: "<<e.what());
		}

		return false;
	}
};

}  // namespace database

}  // namespace app

#endif /* DATABASE_HPP_ */
