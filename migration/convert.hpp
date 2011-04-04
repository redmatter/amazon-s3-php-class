#ifndef CONVERT_HPP
#define CONVERT_HPP

#include "migration_data.hpp"

#include "../archived/database.hpp"
#include "../archived/types.hpp"
#include "../archived/compress.hpp"
#include "../archived/crypto.hpp"

#include "../common/types.hpp"
#include "../common/archivable.hpp"

#include <rml/utils.h>
#include <rml/pc_channel.h>

#include <string>
#include <sstream>
#include <utility>

namespace app {

extern std::string temp_files_base_directory;
extern std::string rmfs_base_directory;

class convert
{
	migration_data::channel_t& inward_channel_;
	migration_data::channel_t& outward_channel_;
	volatile bool& quit_;

	app::impl::crypto crypto_;

	convert(migration_data::channel_t& inward_channel, migration_data::channel_t& outward_channel, volatile bool& quit) :
			inward_channel_(inward_channel), outward_channel_(outward_channel), quit_(quit)
	{
	}

	void run()
	{
		try {
			while(!app::quit) {
				const migration_data& data(inward_channel_.consume());

				const std::string& tmp_prefix(data.identifier+'.');

				data.p_fstream.reset(new rml::utils::tmp_fstream(rmfs_base_directory+'/'+data.path, std::fstream::in | std::fstream::binary));

				rml::utils::tmp_fstream::pointer p_tmp(new rml::utils::tmp_fstream(tmp_prefix, std::size_t(0),
						std::fstream::in | std::fstream::out | std::fstream::binary | std::fstream::trunc, temp_files_base_directory));

				unsigned int iter = 3;
				while(iter && !app::quit) {
					data.p_fstream->seekg(0);

					switch(iter) {

					// compress
					case 3:
					{
						app::bzip2::compress(data.p_fstream, p_tmp, types::file_format::clear_wav, data.identifier);
						data.p_fstream = p_tmp;
						p_tmp.reset(new rml::utils::tmp_fstream(tmp_prefix, std::size_t(0),
								std::fstream::in | std::fstream::out | std::fstream::binary | std::fstream::trunc, temp_files_base_directory));
						break;
					}

					// encrypt
					case 2:
					{
						crypto_.encrypt(data.p_fstream, p_tmp, types::file_format::compressed_wav, data.identifier, data.p_endpoint->encryption_key);
						data.p_fstream = p_tmp;
						p_tmp.reset();
						break;
					}

					// done convert
					case 1:
					{
						data.state = migration_data::converted;
						outward_channel_.produce(data);
						break;
					}

					default:
						break;

					}

					iter--;
				}
			}
		}
		catch(const migration_data::channel_t::finishing_exception& e) {
			LogVerbose(inward_channel_ << e.what());
			return;
		}
	}
};

}

#endif // CONVERT_HPP
