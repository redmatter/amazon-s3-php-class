#ifndef STORAGE_HPP
#define STORAGE_HPP

#include "migration_data.hpp"

#include "../archived/storage_backend.hpp"

namespace app {

class migration_storage : public storage::s3::backend
{
	migration_data::channel_t& channel_copy_to_s3_;
	migration_data::channel_t& channel_copy_to_s3_done_;

	volatile bool& quit_;

public:
	migration_storage(const boost::program_options::variables_map& confMap, migration_data::channel_t& channel_copy_to_s3, migration_data::channel_t& channel_copy_to_s3_done, volatile bool& quit) :
			storage::s3::backend(confMap), channel_copy_to_s3_(channel_copy_to_s3), channel_copy_to_s3_done_(channel_copy_to_s3_done), quit_(quit)
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
				migration_data data = channel_copy_to_s3_.consume();
//				const std::string& tmp_prefix(data.identifier+'.');

				if(data.state != migration_data::error) {

					data.p_fstream->re_open();

					try {
						put(data.p_endpoint, data.identifier, data.p_fstream, data.target_format);
						LogMessage(data.identifier<<": copied to S3");
						data.state = migration_data::copied;
					}
					catch(const std::exception& e) {
						LogError(data.identifier<<": Error saving to S3; "<<e.what());
					}

					data.p_fstream->close();
				}

				pc_produce(channel_copy_to_s3_done_, data);
			}
		}
		catch(const migration_data::channel_t::finishing_exception& e) {
			LogVerbose(channel_copy_to_s3_ << e.what());
			return;
		}
	}
};

}

#endif // STORAGE_HPP
