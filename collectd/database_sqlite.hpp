/*
 * database_sqlite.hpp
 *
 *  Created on: 13/01/2013
 *  Author: dkorah
 */

#ifndef DATABASE_SQLITE_HPP_
#define DATABASE_SQLITE_HPP_

#include "../common/policy.hpp"
#include "../common/archivable.hpp"

#include <rml/sqlite/session.hpp>
#include <rml/sqlite/query.hpp>
#include <rml/sqlite/work.hpp>
#include <rml/exception.h>
#include <rml/logger.h>

#include <boost/program_options.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <set>

#include <time.h>
#include <stdio.h>

namespace {

template < typename ByteArray >
void bytes_from_string(const std::string& str, ByteArray& bytes)
{
	byte* buf(bytes.c_array());
	size_t skip(0);
	for(size_t i = 0; i < std::min<size_t>(str.length()/2, ByteArray::size()); i++) {
		skip += ((str[skip + i * 2] == '-')? 1: 0);
		if(1 != sscanf(&str[skip + i * 2], "%2hhx", &buf[i])) {
			LogWarning("Invalid character found at "<<(i*2));
			throw rml::exception("Invalid character found");
		}
	}
}

} // namespace

namespace app {

namespace database {

using namespace rml;

class internal
{
	const sqlite::session::pool::pointer p_pool_;

	typedef std::list< archiving::archivable::pointer > archivable_pointer_list;

	const unsigned int save_interval_seconds_;
	archivable_pointer_list save_list_;
	boost::recursive_mutex save_list_mutex_;

	boost::atomic_bool shutdown_flagged_;

	inline bool is_shutdown_flagged()
	{
		return shutdown_flagged_.load(boost::memory_order_consume);
	}

public:
	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("sqlite.session_pool_size", boost::program_options::value<unsigned int>()
						->default_value(3), "Max sessions to the queue database")
				("sqlite.session_max_dormant_seconds", boost::program_options::value<unsigned int>()
						->default_value(60), "Max seconds before we kill a free session")
				("sqlite.database_path", boost::program_options::value<std::string>()
						->default_value("/var/lib/archiving/collectd.db3"), "Path to the database file")
				("sqlite.debug_on", boost::program_options::value<bool>()
						->default_value(false), "Database SQL debug on/off")
				("queue.save_interval_seconds", boost::program_options::value<unsigned int>()
						->default_value(5), "Queue save (to db) interval in seconds")
				;
	}

	/**
	 * Get the session pool
	 * @return
	 */
	inline size_t get_pool_size() const
	{
		return p_pool_->size();
	}

	/**
	 * clear dormant sqlite sessions
	 */
	inline void clear_dormant_sessions() const
	{
		p_pool_->clear_dormant_sessions();
	}

	/**
	 * Constructor
	 *
	 * @param confMap
	 */
	internal(const boost::program_options::variables_map& confMap, const char* ignore) :
		p_pool_(sqlite::session::pool::create(
				"Queue",
				confMap["sqlite.session_pool_size"].as<unsigned int>(),
				boost::posix_time::seconds(confMap["sqlite.session_max_dormant_seconds"].as<unsigned int>()),
				confMap["sqlite.database_path"].as<std::string>(),
				sqlite::session::flags_readonly | sqlite::session::flags_cache_shared,
				confMap["sqlite.debug_on"].as<bool>())),
		save_interval_seconds_(confMap["queue.save_interval_seconds"].as<unsigned int>()), shutdown_flagged_(false)
	{
	}

	inline void shutdown()
	{
		shutdown_flagged_ = true;
	}

	bool save(const archiving::archivable::pointer& p_arc) try
	{
		boost::recursive_mutex::scoped_lock lock(save_list_mutex_);

		if(p_arc->set_save_setup())
			return true; // save already in progress from an earlier call to save()

		save_list_.push_back(p_arc);

		return true;
	}
	catch(const std::exception& e) {
		LogError(*p_arc<<" failed to save; exception:"<<e.what());
		return false;
	}
	catch(...) {
		LogError(*p_arc<<" failed to save; exception:unknown");
		return false;
	}

	void save_run() try
	{
		int iter(-21);
		while(!is_shutdown_flagged()) {

			iter++;

			if(iter < 0 || iter % save_interval_seconds_) {
				boost::this_thread::sleep(boost::posix_time::seconds(1));
				continue;
			}

			save_all();

			iter = 1;
		}

		// any pending shall be saved when we quit
		save_all();
		LogMessage("Database-save thread terminating");
	}
	catch(const std::exception& e) {
		LogError("save_run failed; exception:"<<e.what());
	}
	catch(...) {
		LogError("save_run failed; exception:unknown");
	}

private:
	inline void save_all() try
	{
		boost::recursive_mutex::scoped_lock lock(save_list_mutex_);

		if(save_list_.empty())
			return;

		sqlite::work txn(p_pool_, sqlite::work::mode::deffered, sqlite::session::flags_readwrite | sqlite::session::flags_cache_private);

		try {
			for(archivable_pointer_list::iterator itr = save_list_.begin(); itr != save_list_.end(); ) {
				archiving::archivable::pointer p_arc = *itr;
				if(!save_real(p_arc, txn)) {
					LogVerbose(*p_arc<<" real_save() failed; will try again later");
					++itr;
					continue;
				}

				p_arc->reset_save_setup();

				save_list_.erase(itr++);
			}
		}
		catch(const std::exception& e) {
			LogWarning(e.what());
		}

		txn.commit();
	}
	catch(const std::exception& e) {
		LogError("save_all failed; exception:"<<e.what());
	}
	catch(...) {
		LogError("save_all failed; exception:unknown");
	}

	/**
	 * Save archivable
	 *
	 * @param p_arc
	 */
	bool save_real(archiving::archivable::pointer p_arc, const sqlite::work& txn)
	{
		try {
			sqlite::query::pointer p_query(txn.query());
			{
				if(!p_arc->get_database_id()) {
					LogVerbose("Insert: "<<*p_arc);
					p_query << "INSERT INTO `Queue` ("
							" ParentID, FileType,"
							" OrgID, UserIDs,"
							" UUID, OriginatorUUID,"
							" Path, ModifiedPath,"
							" State,"
							" PolicyID, MD5, AddToCDRMunchCache, BufferedRecording, DataFormat,"
							" Retry,"
							" StartEpoch,"
							" UpdateTime,"
                            " LegUUID"
							" ) VALUES ( "
							" :ParentID, :FileType,"
							" :OrgID, :UserIDs,"
							" :UUID, :OriginatorUUID,"
							" :Path, :ModifiedPath,"
							" :State,"
							" :PolicyID, :MD5, :AddToCDRMunchCache, :BufferedRecording, :DataFormat,"
							" :Retry,"
							" :StartEpoch,"
							" DATETIME('NOW'),"
                            " :LegUUID"
							")"
							;
				}
				else {
					LogVerbose("Update "<<*p_arc);
					p_query << "UPDATE `Queue` "
							" SET"
							" ParentID=:ParentID, FileType=:FileType,"
							" OrgID=:OrgID, UserIDs=:UserIDs,"
							" UUID=:UUID, OriginatorUUID=:OriginatorUUID,"
							" Path=:Path, ModifiedPath=:ModifiedPath,"
							" State=:State,"
							" PolicyID=:PolicyID, MD5=:MD5, AddToCDRMunchCache=:AddToCDRMunchCache, BufferedRecording=:BufferedRecording, DataFormat=:DataFormat,"
							" Retry=:Retry,"
							" StartEpoch=:StartEpoch,"
							" UpdateTime=DATETIME('NOW'),"
                            " LegUUID=:LegUUID"
							" WHERE ID=:ID";
				}

				p_query->prepare();
			}

			const std::pair< std::string, std::string >& paths(p_arc->get_paths());
			p_query->bind(paths.first, ":Path");
			if(paths.second.empty())
				p_query->bind(sqlite::null, ":ModifiedPath");
			else
				p_query->bind(paths.second, ":ModifiedPath");

			std::string userIDs_str;
			{
				std::ostringstream ostr;
				const std::vector< archiving::user_id_t >& userIDs( p_arc->get_owners() );
				std::vector< archiving::user_id_t >::const_iterator itr = userIDs.begin();
				for(; itr != userIDs.end(); ++itr) {
					if(itr != userIDs.begin())
						ostr << ',';

					ostr << *itr;
				}
			}

			archiving::uuid_t uuid;
			bytes_from_string(p_arc->get_uuid(), uuid);

			archiving::uuid_t originator_uuid;
			bytes_from_string(p_arc->get_originator_uuid(), originator_uuid);

            archiving::uuid_t leg_uuid;

            // TODO remove ParentID altogether once we are confident with the parent-less version
			p_query->bind<int64_t>(0ULL, ":ParentID")
					->bind(p_arc->get_type(), ":FileType")

					->bind<int>(p_arc->get_org_id(), ":OrgID")
					->bind(userIDs_str, ":UserIDs")

					->bind(sqlite::types::blob(uuid), ":UUID")
					->bind(sqlite::types::blob(originator_uuid), ":OriginatorUUID")

					->bind(p_arc->get_current_state(), ":State")

					->bind(p_arc->get_policy_id(), ":PolicyID")
					->bind(sqlite::types::blob(p_arc->get_md5()), ":MD5")
					->bind(p_arc->get_add_to_cdrmunch_cache()? "true": "false", ":AddToCDRMunchCache")
					->bind(0, ":BufferedRecording") // TODO migration from call-buffering v2 to v1 will need to address this
					->bind(p_arc->get_data_format(), ":DataFormat")

					->bind<int>(p_arc->get_attempt(), ":Retry")

					->bind<int64_t>(p_arc->get_start_epoch(), ":StartEpoch")
					;

            const std::string& str_leg_uuid(p_arc->get_leg_uuid());
            if (str_leg_uuid.empty()) {
                p_query->bind(sqlite::null, ":LegUUID");
            } else {
                bytes_from_string(p_arc->get_leg_uuid(), leg_uuid);
                p_query->bind(sqlite::types::blob(leg_uuid), ":LegUUID");
            }

            if(p_arc->get_database_id())
				p_query->bind<int64_t>(p_arc->get_database_id(), ":ID");

			const sqlite::result& res(p_query->execute());
			if(!p_arc->get_database_id()) {
				LogVerbose(*p_arc<<" ID: "<<res.last_insert_rowid());
				p_arc->set_database_id(res.last_insert_rowid());
			}

			return true;
		}
		catch(const sqlite::exception& e) {
			LogWarning(e);
		}
		catch(const std::exception& e) {
			LogWarning("Exception: "<<e.what());
		}

		return false;
	}

public:
	bool is_queued(archiving::archivable::pointer& p_arc)
	{
		try {
			archiving::archivable& arc(*p_arc);

			archiving::uuid_t uuid;
			bytes_from_string(arc.get_uuid(), uuid);

			sqlite::work txn(p_pool_);

			sqlite::query::pointer p_query(txn.query("SELECT 1 FROM `Queue` WHERE `FileType`=:FileType AND `UUID`=:UUID LIMIT 1"));
			const sqlite::result& res(p_query->prepare()
					->bind(p_arc->get_type(), ":FileType")
					->bind(sqlite::types::blob(uuid), ":UUID")
					->execute());
			return res;
		}
		catch(const sqlite::exception& e) {
			LogWarning(e);
		}
		catch(const std::exception& e) {
			LogWarning("Exception: "<<e.what());
		}

		// fail close ... we dont want to load any file if DB is out of action
		return true;
	}

	/**
	 * Find archivables from DB
	 * ( reloading after restart )
	 *
	 * @param pbx_hostname (ignored)
	 * @param fn_get_loaded_instance
	 * @param fn_produce
	 * @param have_more_to_load
	 */
	void find_archivables(
			const std::string& pbx_hostname,
			const boost::function<archiving::archivable::pointer(const archiving::archivable::pointer&)>& fn_get_loaded_instance,
			const boost::function<void (boost::shared_ptr<archiving::archivable>)>& fn_produce,
			bool& have_more_to_load,
			const float& loading_factor)
	{
		static bool first = true;
		static uint64_t first_max_id = 0;
		static uint64_t max_id = 0;

		try {
			sqlite::work txn(p_pool_);

			if(first) {
				sqlite::query::pointer p_query(
						txn.query("SELECT MAX(`ID`) FROM `Queue` WHERE `Retry` < 5 AND `State` NOT IN (:stateStored, :stateDiscarded)"));

				const sqlite::result& res(p_query->prepare()
						->bind(archiving::archivable::state::stored, ":stateStored")
						->bind(archiving::archivable::state::discarded, ":stateDiscarded")
						->execute());
				if(res && !res[0].is_null())
					first_max_id = res[0].as_int64();

				first = false;
			}

			if(first_max_id && max_id <= first_max_id) {
				sqlite::query::pointer p_query(txn.query("SELECT"
						" ID, FileType, LOWER(HEX(SUBSTR(UUID, 1, 4)) || '-' || HEX(SUBSTR(UUID, 5, 2)) || '-' || HEX(SUBSTR(UUID, 7, 2)) || '-' || HEX(SUBSTR(UUID, 9, 2)) || '-' || HEX(SUBSTR(UUID, 11))) AS UUID "
						 "FROM"
						 " `Queue` "
						 "WHERE"
						 " Retry < 5 AND State NOT IN (:stateStored, :stateDiscarded)"
						 " AND ID <= :FirstMaxID AND ID > :MaxID "
						 "ORDER BY ID ASC "
						 "LIMIT 100"));
				const sqlite::result& res(p_query->prepare()
						->bind(archiving::archivable::state::stored, ":stateStored")
						->bind(archiving::archivable::state::discarded, ":stateDiscarded")
						->bind<int64_t>(first_max_id, ":FirstMaxID")
						->bind<int64_t>(max_id, ":MaxID")

						->execute());

				unsigned int count(0);

				while(res) {
					const uint64_t& id(res[0].as_int64());
					max_id = id;

					archiving::archivable::pointer p_archivable;

					archiving::file_type::file_type_t archivable_file_type = archiving::file_type::file_type_t(res[1].as_int());

					try{
						p_archivable = fn_get_loaded_instance(archiving::archivable::pointer(
								new archiving::archivable(res[2].as_text(), archivable_file_type)));

						if(!p_archivable) {
							LogVerbose("Loading ID:"<<res[0].as_int64()<<" FileType:"<<archiving::file_type::to_string(archivable_file_type)<<" UUID:"<<res[2].as_text());

							p_archivable =  load_archivable_by_id(res[0].as_int64());
							if((loading_factor == 1.0) || ((float(count++ % 10)/10.0) < loading_factor))
								fn_produce(p_archivable);
						}
					}
					catch(const std::exception& e) {
						LogWarning("Exception: "<<e.what());
					}

					res.next();
				}
			}

			have_more_to_load = (first_max_id && (max_id < first_max_id)); // more to load?
		}
		catch(const sqlite::exception& e) {
			LogWarning(e);
		}
		catch(const std::exception& e) {
			LogWarning("Exception: "<<e.what());
		}
	}

	/**
	 * Load archivable by a key/value pair
	 *
	 * @param key
	 * @param value
	 * @return
	 */
	template< typename ValueType >
	archiving::archivable::pointer load_archivable_by_key_value(const std::string& key, const ValueType& value)
	{
		try{
			sqlite::work txn(p_pool_);

			sqlite::query::pointer p_query(txn.query());
			p_query << "SELECT"
					" ParentID," // ParentID deprecated; only for fallback during the "self-migration" period
					" FileType,"
					" OrgID,"
					" LOWER(HEX(SUBSTR(UUID, 1, 4)) || '-' || HEX(SUBSTR(UUID, 5, 2)) || '-' || HEX(SUBSTR(UUID, 7, 2)) || '-' || HEX(SUBSTR(UUID, 9, 2)) || '-' || HEX(SUBSTR(UUID, 11))) AS str_UUID,"
					" Path,"
					" ModifiedPath,"
					" State,"
					" PolicyID,"
					" HEX(MD5),"
					" AddToCDRMunchCache,"
					" DataFormat,"
					" Retry,"
					" StartEpoch,"
					" 'localhost',"
					" LOWER(HEX(SUBSTR(OriginatorUUID, 1, 4)) || '-' || HEX(SUBSTR(OriginatorUUID, 5, 2)) || '-' || HEX(SUBSTR(OriginatorUUID, 7, 2)) || '-' || HEX(SUBSTR(OriginatorUUID, 9, 2)) || '-' || HEX(SUBSTR(OriginatorUUID, 11))) AS OrigUUID,"
					" UserIDs,"
					" ID,"
					" BufferedRecording,"
                    " LOWER(HEX(SUBSTR(LegUUID, 1, 4)) || '-' || HEX(SUBSTR(LegUUID, 5, 2)) || '-' || HEX(SUBSTR(LegUUID, 7, 2)) || '-' || HEX(SUBSTR(LegUUID, 9, 2)) || '-' || HEX(SUBSTR(LegUUID, 11))) AS str_LegUUID "
					"FROM Queue "
					"WHERE "<<key<<" = :value";
			const sqlite::result& res(p_query->prepare()
					->bind(value, ":value")
					->execute());
			if(res) {
                const archiving::file_type::file_type_t type(static_cast<archiving::file_type::file_type_t>(res[1].as_int()));
				archiving::archivable::pointer p_arc(new archiving::archivable(
						res[3].as_text(),
						res[14].is_null()? "": res[14].as_text(),
						res[12].as_int64(),
						res[4].as_text(),
						type,
						res[10].as_text(),
						res[13].as_text(),
						app::policy::id_type(res[7].as_int64()),
						res[9].as_int(),
						!res[5].is_null(),
						res[5].is_null()? "": res[5].as_text(),
						res[8].as_text(),
						archiving::archivable::state::state_t(res[6].as_int())
//						, res[17].as_int() // BufferedRecording
					));

				p_arc->set_database_id(res[16].as_int64());
				archiving::owner_info owner(archiving::org_id_t(res[2].as_int()));
				if(!res[15].is_null()) {
					const std::string& userIds(res[15].as_text());
					std::istringstream str(userIds);
					std::string userId_str;
					while(std::getline(str, userId_str, ','))
						owner.addUser((archiving::user_id_t)rml::utils::strtoull(userId_str.c_str(), 0ULL));
					p_arc->set_owners(owner);
				}

                // If LegUUID is empty and the file-type is not CDR and parent-id > 0, assume it to be an old record and
                // get LegUUID from parent-id. In newer records, parent-id is set to zero.
                // LegUUIDs loaded this way are saved later on into the db-record.
                const uint64_t parent_id = static_cast<uint64_t>(res[0].as_int64());
                std::string str_leg_uuid(res[18].is_null()? "": res[18].as_text());
                if (str_leg_uuid.empty() && type != archiving::file_type::cdr && parent_id > 0) {
                    LogMessage(p_arc<<" Archivable found with NO LegUUID but WITH a ParentID ("<<parent_id<<"); looking up parent for LegUUID");
                    try {
                        sqlite::work txn_leg_uuid(p_pool_);

                        sqlite::query::pointer p_query_leg_uuid(txn_leg_uuid.query());
                        p_query_leg_uuid << "SELECT"
                                " LOWER(HEX(SUBSTR(UUID, 1, 4)) || '-' || HEX(SUBSTR(UUID, 5, 2)) || '-' || HEX(SUBSTR(UUID, 7, 2)) || '-' || HEX(SUBSTR(UUID, 9, 2)) || '-' || HEX(SUBSTR(UUID, 11))) AS str_UUID "
                                " FROM Queue"
                                " WHERE ID = :ParentID AND FileType = :CdrFileType";
                        const sqlite::result& res_leg_uuid(p_query->prepare()
                                ->bind(static_cast<int64_t>(parent_id), ":ParentID")
                                ->bind(static_cast<int>(archiving::file_type::cdr), ":CdrFileType")
                                ->execute());
                        if(res_leg_uuid) {
                            str_leg_uuid = res_leg_uuid[0].as_text();
                        }
                    } catch (...) {
                        /* ignore any exceptions here */
                    }
                }

                p_arc->set_leg_uuid(str_leg_uuid);

                return p_arc;
			}
		}
		catch(const sqlite::exception& e) {
			LogWarning(e);
		}
		catch(const std::exception& e) {
			LogWarning("Exception: "<<e.what());
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
		return load_archivable_by_key_value("ID", int64_t(id));
	}

	/**
	 * Load archivable by its uuid
	 *
	 * @param uuid
	 * @return
	 */
	archiving::archivable::pointer load_archivable_by_uuid(const std::string &uuid)
	{
		archiving::uuid_t uuid_bin;
		bytes_from_string(uuid, uuid_bin);

		return load_archivable_by_key_value<sqlite::types::blob>("UUID", sqlite::types::blob(uuid_bin));
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
	 * @param pbx_hostname
	 * @param done_items_max_age_minutes
	 * @param max_age_days
	 * @param limit
	 * @return
	 */
	purgedef::pointer_vector purge_find(const std::string& pbx_hostname, const unsigned int done_items_max_age_minutes, const unsigned int max_age_days, const unsigned int limit)
	{
		// TODO XXX isnt list better here?
		purgedef::pointer_vector ret;

		try {
			time_t max_age_days__boundary = time(0) - (max_age_days * 24 * 60 * 60);

			sqlite::work txn(p_pool_);

			sqlite::query::pointer p_query(txn.query());
			p_query <<"SELECT"
					" ID, FileType, LOWER(HEX(SUBSTR(UUID, 1, 4)) || '-' || HEX(SUBSTR(UUID, 5, 2)) || '-' || HEX(SUBSTR(UUID, 7, 2)) || '-' || HEX(SUBSTR(UUID, 9, 2)) || '-' || HEX(SUBSTR(UUID, 11))) AS UUID, Path, ModifiedPath "
					"FROM `Queue` "
					"WHERE ( State IN ( :stateStored, :stateDiscarded ) AND DATETIME('NOW', :DoneMaxAge || ' MINUTE') > UpdateTime ) OR :MaxAgeDays > StartEpoch";
			if(limit)
				p_query << " LIMIT "<<limit;

			const sqlite::result& res(p_query->prepare()
					->bind(archiving::archivable::state::stored, ":stateStored")
					->bind(archiving::archivable::state::discarded, ":stateDiscarded")
					->bind<int>(-int(done_items_max_age_minutes), ":DoneMaxAge")
					->bind<int64_t>(max_age_days__boundary, ":MaxAgeDays")

					->execute());

			DevDebug("stateStored:"<<archiving::archivable::state::stored<<" stateDiscarded:"<<archiving::archivable::state::discarded<<" DoneMaxAge:"<<(-int(done_items_max_age_minutes))<<" MaxAgeDays:"<<max_age_days__boundary);
			while(res) {
				ret.push_back(
						purgedef::pointer(
								new purgedef(
										res[0].as_int64(), archiving::file_type::file_type_t(res[1].as_int()),
										res[2].as_text(), res[3].as_text(), (res[4].is_null()? "": res[4].as_text()))));

				res.next();
			}
		}
		catch(const sqlite::exception& e) {
			LogWarning(e);
		}
		catch(const std::exception& e) {
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
		if(purgeables.empty())
			return true;

		try {
			int xx = 1;

			sqlite::work txn(p_pool_, sqlite::work::mode::deffered, sqlite::session::flags_readwrite | sqlite::session::flags_cache_private);

			sqlite::query::pointer p_query(txn.query());

			p_query << "DELETE FROM `Queue` WHERE ID IN (";
			for(purgedef::pointer_vector::const_iterator itr = purgeables.begin();
					itr != purgeables.end(); ++itr) {
				if(itr != purgeables.begin())
					p_query << ", ";

				p_query << (*itr)->id << "/* "<<archiving::file_type::to_string((*itr)->file_type)<<'/'<<(*itr)->uuid<<" */"<<((0 == (xx++ % 4))? '\n': ' ');
			}

			p_query << ')';

			const sqlite::result& res(p_query->prepare()->execute());
			LogMessage(res.changes<<" rows deleted.");

			txn.commit();

			return true;
		}
		catch(const sqlite::exception& e) {
			LogWarning(e);
		}
		catch(const std::exception& e) {
			LogWarning("Exception: "<<e.what());
		}

		return false;
	}
};

}  // namespace database

}  // namespace app

#endif /* DATABASE_SQLITE_HPP_ */
