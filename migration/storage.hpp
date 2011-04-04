#ifndef STORAGE_HPP
#define STORAGE_HPP

#include "migration_data.hpp"

#include "../archived/storage_backend.hpp"

namespace app {

class migration_storage
{
	migration_data::channel_t& inward_channel_;
	migration_data::channel_t& outward_channel_;

	storage::s3::backend backend_;

	volatile bool& quit_;

	migration_storage(const boost::program_options::variables_map& confMap, migration_data::channel_t& inward_channel, migration_data::channel_t& outward_channel, storage::s3::endpoint::cached_finder_t& cached_s3endpoint_finder, volatile bool& quit) :
			inward_channel_(inward_channel), outward_channel_(outward_channel), cached_s3endpoint_finder_(cached_s3endpoint_finder), backend_(confMap), quit_(quit)
	{
	}


	void run()
	{
		try {
			while(!app::quit) {
				const migration_data& data(inward_channel_.consume());
				const std::string& tmp_prefix(data.identifier+'.');

				try {
					backend_.put(data.p_endpoint, data.identifier, data.p_fstream, data.target_format);
					LogMessage(data.identifier<<": copied to S3");
					data.state = migration_data::copied;
				}
				catch(const std::exception& e) {
					LogError(data.identifier<<": Error saving to S3; "<<e.what());
				}

				outward_channel_.produce(data);
			}
		}
		catch(const migration_data::channel_t::finishing_exception& e) {
			LogVerbose(inward_channel_ << e.what());
			return;
		}
	}
};

}

#endif // STORAGE_HPP
