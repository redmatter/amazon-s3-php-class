#ifndef STORAGE_HPP
#define STORAGE_HPP

#include "migration_data.hpp"

#include "../archived/storage_backend.hpp"

namespace app {

class migration_storage : public storage::s3::backend
{
	migration_data::channel_t& inward_channel_;
	migration_data::channel_t& outward_channel_;

	volatile bool& quit_;

public:
	migration_storage(const boost::program_options::variables_map& confMap, migration_data::channel_t& inward_channel, migration_data::channel_t& outward_channel, volatile bool& quit) :
			storage::s3::backend(confMap), inward_channel_(inward_channel), outward_channel_(outward_channel), quit_(quit)
	{
	}

	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		storage::s3::backend::describe_config(optsDesc);
//		optsDesc.add_options()
//				;
	}

	void run()
	{
		try {
			while(!app::quit) {
				migration_data data = inward_channel_.consume();
//				const std::string& tmp_prefix(data.identifier+'.');

				if(data.state != migration_data::error) {
					try {
						put(data.p_endpoint, data.identifier, data.p_fstream, data.target_format);
						LogMessage(data.identifier<<": copied to S3");
						data.state = migration_data::copied;
					}
					catch(const std::exception& e) {
						LogError(data.identifier<<": Error saving to S3; "<<e.what());
					}
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
