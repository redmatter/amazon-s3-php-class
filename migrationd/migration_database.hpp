#ifndef MIGRATION_DATABASE_HPP
#define MIGRATION_DATABASE_HPP

#include "migration_data.hpp"
#include "file_verify.hpp"

#include "../archived/database.hpp"
#include "../archived/policy.hpp"

#include <rml/utils.h>

#include <boost/algorithm/string.hpp>
#include <boost/random.hpp>
#include <boost/generator_iterator.hpp>

namespace app {

extern std::string hostname;

namespace database {

/**
 * The migration SQLs wrapper
 */
class migration : public basic_database< migration >
{
	const boost::program_options::variables_map& confMap_;

	migration_data::busy_set_t& busied_set_;

	migration_data::channel_t& new_migratables_channel_;
	migration_data::channel_t& done_migratables_channel_;
	migration_data::channel_t& retention_channel_;

	file_verify::data::channel_t& verify_channel_;

	const policy::cached_finder_t& cached_policy_finder_;
	const storage::s3::endpoint::cached_finder_t& cached_s3endpoint_finder_;

	volatile bool& quit_;
	const unsigned int host_count_;
	const unsigned int host_index_;
	const unsigned int poll_interval_seconds_;
	const unsigned int flush_interval_seconds_;

	std::vector<archiving::org_id> orgs_enabled_;

	app::database::retention retention_db_;
public:
	/**
	 * Who am I
	 *
	 * @return string as identifier
	 */
	static inline const char* get_identifier()
	{
		static const char* identifier = "migration";
		return identifier;
	}

	/**
	 * describe configuration
	 *
	 * @param optsDesc
	 */
	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		basic_database<migration>::describe_config(optsDesc);
		retention::describe_config(optsDesc);
		optsDesc.add_options()
				("migration_data.poll_interval_seconds", boost::program_options::value<unsigned int>()->default_value(300U), "migration data polling interval")
				("migration_data.flush_interval_seconds", boost::program_options::value<unsigned int>()->default_value(10U), "migration data - status update flush interval")
				("migration_data.host_count", boost::program_options::value<unsigned int>(), "migration hosts count")
				("migration_data.host_index", boost::program_options::value<unsigned int>(), "migration hosts index")
				("migration_data.orgs_enabled", boost::program_options::value<std::string>()->default_value("1"), "migration enabled for org-ids (comma separated list)")
				;
	}

	/**
	 * Constructor
	 */
	migration(
			const boost::program_options::variables_map& confMap,
			migration_data::busy_set_t& busied_set,
			migration_data::channel_t& new_migratables_channel, migration_data::channel_t& done_migratables_channel, migration_data::channel_t& retention_channel,
			file_verify::data::channel_t& verify_channel,
			const policy::cached_finder_t& cached_policy_finder, const storage::s3::endpoint::cached_finder_t& cached_s3endpoint_finder,
			volatile bool& quit) :

					basic_database< migration >(confMap), confMap_(confMap),
					busied_set_(busied_set),
					new_migratables_channel_(new_migratables_channel), done_migratables_channel_(done_migratables_channel), retention_channel_(retention_channel),
					verify_channel_(verify_channel),
					cached_policy_finder_(cached_policy_finder),
					cached_s3endpoint_finder_(cached_s3endpoint_finder),
					quit_(quit),
					host_count_(confMap.count("migration_data.host_count")? confMap["migration_data.host_count"].as<unsigned int>(): throw rml::exception("migration_data.host_count not defined")),
					host_index_(confMap.count("migration_data.host_index")? confMap["migration_data.host_index"].as<unsigned int>(): throw rml::exception("migration_data.host_index not defined")),
					poll_interval_seconds_(confMap["migration_data.poll_interval_seconds"].as<unsigned int>()),
					flush_interval_seconds_(confMap["migration_data.flush_interval_seconds"].as<unsigned int>()),
					retention_db_(confMap)
	{
		if(confMap.count("migration_data.orgs_enabled")) {
			const std::string& orgs_enabled(confMap["migration_data.orgs_enabled"].as<std::string>());
			if(!orgs_enabled.empty()) {
				std::vector<std::string> orgs_enabled_str;
				boost::algorithm::split(orgs_enabled_str, orgs_enabled, boost::is_any_of(", \t"));
				std::vector<std::string>::const_iterator end = orgs_enabled_str.end();
				for(std::vector<std::string>::const_iterator itr = orgs_enabled_str.begin(); itr != end; ++itr)
					orgs_enabled_.push_back(rml::utils::strtoull(itr->c_str()));
			}
		}
	}

	/**
	 * Find items to migrate
	 */
	void find_migratables()
	{
		int c = -5;

		while(!quit_) {

			if( ++c % poll_interval_seconds_) {
				boost::this_thread::sleep(boost::posix_time::seconds(1));
				continue;
			}

			try {
				const std::size_t& channel_count(new_migratables_channel_.count());
				if(channel_count >= 200) {
					LogMessage(channel_count<<" unprocessed items; not loading any more");
					continue;
				}

				rml::MySQLConnection conn(pool_);
				mysqlpp::Query query = conn->query();
				{
					query << "SELECT ID, LCASE(CONCAT(HEX(SUBSTR(UUID, 1, 4)),'-',HEX(SUBSTR(UUID, 5, 2)),'-',HEX(SUBSTR(UUID, 7, 2)),'-',HEX(SUBSTR(UUID, 9, 2)),'-',HEX(SUBSTR(UUID, 11)))) `UUID`, Path, OrgID, UserID, CreateEpoch "
					"FROM Migration "
					"WHERE ";
					if(!orgs_enabled_.empty()) {
						if(1 == orgs_enabled_.size()) {
							query << "OrgID="<<orgs_enabled_[0]<<" AND ";
						}
						else {
							int c = 0;
							std::vector<archiving::org_id>::const_iterator end = orgs_enabled_.end();
							query << "OrgID IN (";
							for(std::vector<archiving::org_id>::const_iterator itr = orgs_enabled_.begin(); itr != end; ++itr) {
								query << (c? ',': ' ')<<*itr;
								c++;
							}
							query << ") AND ";
						}
					}
					query << "FileState='ok' AND Status IN ('pending','retry') AND Type='recordings'"
							"AND ( GoodHost=%0q:HostName OR ( ISNULL(GoodHost) AND ( ( ID % %1:HostCount ) = %2:HostIndex) ) ) LIMIT "<<(200 - channel_count);
					query.parse();
				}
				mysqlpp::SQLQueryParms qp;
				qp << app::hostname << host_count_ << host_index_;
				LogDebug("SQL: "<<query.str(qp));
				mysqlpp::UseQueryResult res = query.use(qp);
				while(mysqlpp::Row row = res.fetch_row()) {
					// make migration_data
					archiving::org_id org = rml::utils::strtoull(row[3].c_str());
					new_migratables_channel_.produce(
							migration_data(
									rml::utils::strtoull(row[0].c_str()),
									row[1].c_str(),
									row[2].c_str(),
									org,
									rml::utils::strtoull(row[4].c_str()),
									rml::utils::strtoull(row[5].c_str()),
									cached_s3endpoint_finder_(org),
									app::types::file_format::encrypted_compressed_wav,
									archiving::archivable::file_type::recording));
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


	struct status_update
	{
		uint64_t id;
		migration_data::state_t state;

		struct compare
		{
			bool operator()(const status_update& lhs, const status_update& rhs)
			{
				return lhs.state < rhs.state;
			}
		};

		status_update(const migration_data& md) : id(md.id), state(md.state)
		{
		}

		typedef std::multiset<status_update, status_update::compare> multiset_t;
	};

	/**
	 * Update migration status
	 * TODO batch flush
	 */
	void update_status()
	{
		int c = -15;

		while(!quit_) {

			if( ++c % flush_interval_seconds_) {
				boost::this_thread::sleep(boost::posix_time::seconds(1));
				continue;
			}

			unsigned int batch_max = 1024;
			status_update::multiset_t status_updates;
			try {
				while(batch_max--) {
					const migration_data& data(done_migratables_channel_.consume(1));
					status_updates.insert(status_update(data));
					if(data.state == migration_data::copied)
						// add to retention queue
						retention_channel_.produce(data);
				}
			}
			catch(const migration_data::channel_t::consume_timed_out_exception& e) {
				LogMessage(done_migratables_channel_<< ": "<<e.what()<<"; flusing any status updates collected");
			}
			catch(const std::exception& e) {
				LogError("Exception: "<<e.what());
			}
			catch(...) {
				LogError("Unexpected exception");
			}

			try {
				rml::MySQLConnection conn(pool_);
				while(status_updates.begin() != status_updates.end()) {
					mysqlpp::Transaction transaction(*conn, false);
					status_update::multiset_t::iterator b_itr = status_updates.begin();
					std::pair< status_update::multiset_t::iterator, status_update::multiset_t::iterator > range = status_updates.equal_range(*b_itr);

					mysqlpp::Query query = conn->query();
					const migration_data::state_t& state_batch(b_itr->state);
					switch(state_batch) {
					case migration_data::copied:
						query << "UPDATE Migration SET Status='migrated' WHERE ID IN (";
						break;

					case migration_data::error:
						query << "UPDATE Migration SET Status='migration_error' WHERE ID IN (";
						break;

					default:
						query << "UPDATE Migration SET Status=IF(Status='retry', 'migration_error', 'retry') WHERE ID IN (";
						break;
					}

					int values_count = 0;
					for(status_update::multiset_t::iterator itr = range.first; itr != range.second; ++itr) {
						query << (values_count? ',': ' ')<<itr->id;
						values_count++;
					}
					query << ')';
					std::cout << "SQL: "<<query.str() << std::endl;
					mysqlpp::SimpleResult res = query.execute();
					LogMessage("Status updates : "<<res.rows()<<" rows updated");

					status_updates.erase(range.first, range.second);

					transaction.commit();
				}
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

	/**
	 * flush_retention
	 * polls the channel for items to be flushed
	 */
	void flush_retention()
	{
		boost::mt19937 rng;
		boost::uniform_int<> one_to_1000( 1, 1000 );
		boost::variate_generator< boost::mt19937, boost::uniform_int<> > rand_one_to_1000(rng, one_to_1000);

		// start in 20s
		int c = -20;

		while(!quit_) {

			if( ++c % flush_interval_seconds_) {
				boost::this_thread::sleep(boost::posix_time::seconds(1));
				continue;
			}

			database::retention::data::multiset_t retention_set;
			database::retention::usage_data::multiset_t usage_data_set;

			try {
				while(retention_set.size() < 1024) {
					try {
						const migration_data& data(retention_channel_.consume(1));

						{
							database::retention::data retention;
							retention.file_format = data.target_format;
							retention.file_type = data.filetype;
							retention.org = data.org;
							retention.user = data.user;
							retention.uuid = data.uuid;
							retention.retention = app::applied_policy(cached_policy_finder_(std::make_pair(data.org, policy::id_type(0))), data.create_time);
							retention.endpoint_id = data.p_endpoint->identifier;

							retention_set.insert(retention);
						}

						{
							database::retention::usage_data usage;
							usage.org = data.org;
							usage.filetype = data.filetype;
							usage.create_time = data.create_time;
							usage.clear_size = data.clear_size;
							usage.real_size = data.real_size;

							usage_data_set.insert(usage);
						}

						busied_set_.remove(data);

						{
							file_verify::data verify_data;
							verify_data.md = data;
							verify_data.random = rand_one_to_1000();

							verify_channel_.produce(verify_data);
						}
					}
					catch(const migration_data::channel_t::consume_timed_out_exception& e) {
						if(!retention_set.empty())
							LogVerbose("consume timed-out; flush items we might already have...");
						break;
					}
				}
			}
			catch(const migration_data::channel_t::finishing_exception& e) {
				LogMessage(retention_channel_<<": finishing; flushing any remaining");
				const migration_data::channel_t::item_set& data_set(retention_channel_.consume_remaining());
				migration_data::channel_t::item_set::const_iterator end = data_set.end();
				for(migration_data::channel_t::item_set::const_iterator itr = data_set.begin(); itr != end; ++itr) {
					{
						database::retention::data retention;
						retention.file_format = itr->target_format;
						retention.file_type = itr->filetype;
						retention.org = itr->org;
						retention.user = itr->user;
						retention.uuid = itr->uuid;
						retention.retention = app::applied_policy(cached_policy_finder_(std::make_pair(itr->org, policy::id_type(0))), itr->create_time);
						retention.endpoint_id = itr->p_endpoint->identifier;

						retention_set.insert(retention);
					}

					{
						database::retention::usage_data usage;
						usage.org = itr->org;
						usage.filetype = itr->filetype;
						usage.create_time = itr->create_time;
						usage.clear_size = itr->clear_size;
						usage.real_size = itr->real_size;

						usage_data_set.insert(usage);
					}
				}
				break;
			}
			catch(const std::exception& e) {
				LogError("Exception: "<<e.what());
			}
			catch(...) {
				LogError("Unexpected exception");
			}

			retention_db_.save(retention_set, usage_data_set);
		}

		LogMessage("flush_retention(): thread ending");
	}
};

}

}

#endif // MIGRATION_DATABASE_HPP
