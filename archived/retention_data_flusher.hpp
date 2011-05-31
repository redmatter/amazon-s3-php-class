/*
 * retention_data_flusher.hpp
 *
 *  Created on: 26 May 2011
 *      Author: dkorah
 */

#ifndef RETENTION_DATA_FLUSHER_HPP_
#define RETENTION_DATA_FLUSHER_HPP_

#include "database.hpp"

namespace app {

class retention_data_flusher
{
	operation_data::channel_t& retention_data_channel_;
	database::internal& internal_db_;

	unsigned int flush_interval_seconds_;

	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("retention_data.flush_interval_seconds", boost::program_options::value<unsigned int>()->default_value(10U), "retention data - flush interval in seconds")
				;
	}

	retention_data_flusher(const boost::program_options::variables_map& confMap, operation_data::channel_t& retention_data_channel, database::internal& internal_db) :
		retention_data_channel_(retention_data_channel),
		internal_db_(internal_db),
		flush_interval_seconds_(confMap["retention_data.flush_interval_seconds"].as<unsigned int>())
	{
	}

	void run()
	{
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
				unsigned int batch_max = 500;
				while(!quit_ && batch_max--) {
					try {
						operation_data::pointer data = retention_data_channel_.consume(0, 1000);
						const archiving::archivable::identifier& identifier(data->p_detail->identifier);

						time_t creation_time;
						{
							const types::metadata::pointer& p_metadata( data->p_detail->p_metadata );
							types::metadata::const_iterator itr = p_metadata->find("CreateTime");
							if(itr == p_metadata->end()) {
								LogWarning("CreateTime NOT found in metadata");
								time(&creation_time);
							}
							else
								creation_time = rml::utils::strtoull(itr->second.c_str());
						}

						if(data->state == operation_state::put_file_save_ok) {
							LogDebug(identifier<<": ToRetention");
							{
								database::retention::data retention;
								retention.file_format = data->p_detail->target_file_format;
								retention.file_type = identifier.filetype;
								retention.org = identifier.org;
								retention.uuid = identifier.uuid;
								retention.retention = app::applied_policy(data->p_detail->policy, creation_time);
								retention.endpoint_id = data->p_detail->p_endpoint->identifier;
								retention.user = 0;
								{
									// TODO solve multi user situation
									const types::metadata::pointer& p_metadata( data->p_detail->p_metadata );
									types::metadata::const_iterator itr = p_metadata->find("UserIDs");
									if(itr != p_metadata->end()) {
										std::string user(itr->second, 0, itr->second.find(','));
										retention.user = rml::utils::strtoll(user.c_str());
									}
								}

								retention_set.insert(retention);
							}

							{
								database::retention::usage_data usage;
								usage.org = identifier.org;
								usage.filetype = identifier.filetype;
								usage.create_time = creation_time;
								usage.clear_size = data->p_detail->clear_size;
								usage.real_size = data->p_detail->real_size;

								usage_data_set.insert(usage);
							}

							data.state = migration_data::retention_info_saved;
						}

						channel_save_status_.produce(data);
					}
					catch(const migration_data::channel_t::consume_timed_out_exception& e) {
						break;
					}
				}

				try {
					retention_db_.save(retention_set, usage_data_set);
				}
				catch(const mysqlpp::Exception& e) {
					LogCritical("Database Error: "<<e.what());
					LogWarning("Raising quit flag due to database error!!");
					quit_ = true;
				}
			}
			catch(const migration_data::channel_t::finishing_exception& e) {
				break;
			}
			catch(const std::exception& e) {
				LogError("Exception: "<<e.what());
			}
			catch(...) {
				LogError("Unexpected exception");
			}
		}

		LogMessage("flush_retention(): thread ending");
	}
};

}  // namespace app

#endif /* RETENTION_DATA_FLUSHER_HPP_ */
