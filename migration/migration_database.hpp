#ifndef MIGRATION_DATABASE_HPP
#define MIGRATION_DATABASE_HPP

#include "migration_data.hpp"

#include "../archived/database.hpp"
#include "../archived/policy.hpp"

#include <rml/utils.h>

namespace app {

extern std::string hostname;

namespace database {

class migration : public basic_database< migration >
{
	const boost::program_options::variables_map& confMap_;

	migration_data::channel_t& new_migratables_channel_;
	migration_data::channel_t& done_migratables_channel_;
	migration_data::channel_t& retention_channel_;

	const policy::cached_finder_t& cached_policy_finder_;

	volatile bool& quit_;
	const unsigned int host_count_;
	const unsigned int host_index_;
	const unsigned int poll_interval_seconds_;
	const unsigned int flush_interval_seconds_;

	app::database::retention retention_db_;
public:
	static inline const char* get_identifier()
	{
		static const char* identifier = "migration";
		return identifier;
	}

	migration(
			const boost::program_options::variables_map& confMap,
			operation_data::channel_t& new_migratables_channel, operation_data::channel_t& done_migratables_channel, migration_data::channel_t& retention_channel,
			const policy::cached_finder_t& cached_policy_finder,
			volatile bool& quit) :

					basic_database< migration >(confMap), confMap_(confMap),
					new_migratables_channel_(new_migratables_channel), done_migratables_channel_(done_migratables_channel), retention_channel_(retention_channel),
					cached_policy_finder_(cached_policy_finder),
					quit_(quit),
					host_count_(confMap.count("migration_data.host_count")? confMap["migration_data.host_count"].as<unsigned int>(): throw rml::exception("migration_data.host_count not defined")),
					host_index_(confMap.count("migration_data.host_index")? confMap["migration_data.host_index"].as<unsigned int>(): throw rml::exception("migration_data.host_index not defined")),
					poll_interval_seconds_(confMap["migration_data.poll_interval_seconds"].as<unsigned int>()),
					flush_interval_seconds_(confMap["migration_data.flush_interval_seconds"].as<unsigned int>()),
					retention_db_(confMap)
	{
	}

	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		app::database::retention::describe_config(optsDesc);
		optsDesc.add_options()
				("migration_data.poll_interval_seconds", boost::program_options::value<unsigned int>()->default_value(300U), "migration data polling interval")
				("migration_data.flush_interval_seconds", boost::program_options::value<unsigned int>()->default_value(10U), "migration data - status update flush interval")
				("migration_data.host_count", boost::program_options::value<unsigned int>(), "migration hosts count")
				("migration_data.host_index", boost::program_options::value<unsigned int>(), "migration hosts index")
				;
	}

	void find_migratables()
	{
		int c = -10;

		while(!quit_) {

			if( ++c % poll_interval_seconds_) {
				boost::this_thread::sleep(boost::posix_time::seconds(1));
				continue;
			}

			try {
				rml::MySQLConnection conn(pool_);
				mysqlpp::Query query = conn->query();
				query << "SELECT ID, UUID, Path, OrgID, CreateEpoch "
				"FROM Migration "
				"WHERE FileState='ok' AND Status='pending' AND Type='recording'"
					"AND ( GoodHost=%0q:HostName OR ( ISNULL(GoodHost) AND ( ( ID % %1:HostCount ) = %2:HostIndex) ) ) LIMIT 200";
				query.parse();
				mysqlpp::SQLQueryParms qp;
				qp << app::hostname << host_count_ << host_index_;
				mysqlpp::UseQueryResult res = query.use(qp);
				while(mysqlpp::Row row = res.fetch_row()) {
					// make migration_data
					new_migratables_channel_.produce(
							migration_data(
									rml::utils::strtoull(row[0].c_str()),
									row[1].c_str(),
									row[2].c_str(),
									rml::utils::strtoull(row[3].c_str()),
									rml::utils::strtoull(row[4].c_str()),
									app::types::file_format::encrypted_compressed_wav,
									archiving::archivable::file_type::call_recording));
				}
			}
			catch(const std::exception& e) {
				LogError("Exception: "<<e.what());
			}
			catch(...) {
				LogError("Unexpected exception");
			}
		}
	}

	void update_status()
	{
		int c = -15;

		while(!quit_) {

			if( ++c % flush_interval_seconds_) {
				boost::this_thread::sleep(boost::posix_time::seconds(1));
				continue;
			}

			try {
				while(true) {
					const migration_data& data(done_migratables_channel_.consume(1));

					try {
						rml::MySQLConnection conn(pool_);

						mysqlpp::Transaction transaction(conn);
						std::string cur_status;
						{
							mysqlpp::Query query = conn->query();
							query << "SELECT ID, Status FROM Migration WHERE ID=%1:ID FOR UPDATE";
							mysqlpp::StoreQueryResult res = query.store();
							cur_status = res[0][1].c_str();
						}

						std::string new_status = ((data.state == migration_data::copied)? "migrated" : ((cur_status == "retry")? "migration_error": "retry"));

						mysqlpp::Query query = conn->query();
						query << "UPDATE Migration SET Status=%0q:Status WHERE ID=%1:ID";
						query.parse();
						mysqlpp::SQLQueryParms qp;
						qp << new_status << data.id;
						mysqlpp::SimpleResult res = query.execute(qp);
						if(res.rows())
							LogMessage(data.identifier<<" [id:"<<data.id<<"] marked as "<<new_status);

						transaction.commit();

						if(data.state == migration_data::copied)
							// add to retention queue
							retention_channel_.produce(data);
					}
					catch(const mysqlpp::Exception& e) {
						LogCritical("Database Error: "<<e.what());
						LogWarning("Raising quit flag due to database error!!");
						quit_ = true;
					}
					catch(const std::exception& e) {
						LogError("Exception: "<<e.what());
					}
					catch(...) {
						LogError("Unexpected exception");
					}
				}
			}
			catch(const migration_data::channel_t::consume_timed_out_exception& e) {
				LogMessage(done_migratables_channel_<< ": No data to flush; nap time!")
			}
			catch(const std::exception& e) {
				LogError("Exception: "<<e.what());
			}
			catch(...) {
				LogError("Unexpected exception");
			}
		}
	}

	void flush_retention()
	{
		int c = -20;

		while(!quit_) {

			if( ++c % flush_interval_seconds_) {
				boost::this_thread::sleep(boost::posix_time::seconds(1));
				continue;
			}

			database::retention::data_set retention_set;

			try {
				while(retention_set.size() < 1024) {
					try {
						const migration_data& data(retention_channel_.consume(1));

						database::retention::data retention;
						retention.file_format = data.target_format;
						retention.org = data.org;
						retention.user = 0;
						retention.uuid = data.uuid;
						retention.retention = app::applied_policy(cached_policy_finder_(std::make_pair(data.org, policy::id_type(0))), data.create_time);
						std::pair<database::retention::data_set::iterator, bool> res = retention_set.insert(retention);
						if(!res.second)
							LogWarning("Adding item to retention_set ignored; Duplicates ?!?");
					}
					catch(const migration_data::channel_t::consume_timed_out_exception& e) {
						break;
					}
				}
			}
			catch(const migration_data::channel_t::finishing_exception& e) {
				LogMessage(retention_channel_<<": finishing; flushing any remaining");
				const migration_data::channel_t::item_set& data_set(retention_channel_.consume_remaining());
				migration_data::channel_t::item_set::const_iterator end = data_set.end();
				for(migration_data::channel_t::item_set::const_iterator itr = data_set.begin(); itr != end; ++itr) {
					database::retention::data retention;
					retention.file_format = itr->target_format;
					retention.org = itr->org;
					retention.user = 0;
					retention.uuid = itr->uuid;
					retention.retention = app::applied_policy(cached_policy_finder_(std::make_pair(itr->org, policy::id_type(0))), itr->create_time);
					std::pair<database::retention::data_set::iterator, bool> res = retention_set.insert(retention);
					if(!res.second)
						LogWarning("Adding item to retention_set ignored; Duplicates ?!?");
				}
				break;
			}
			catch(const std::exception& e) {
				LogError("Exception: "<<e.what());
			}
			catch(...) {
				LogError("Unexpected exception");
			}

			retention_db_.save(retention_set);
		}

		LogMessage("flush_retention(): thread ending");
	}
}

}

}

#endif // MIGRATION_DATABASE_HPP
