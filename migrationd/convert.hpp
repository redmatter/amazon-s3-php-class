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

/**
 * Convert file
 */
class convert
{
	migration_data::channel_t& channel_convert_;
	migration_data::channel_t& channel_copy_to_s3_;
	volatile bool& quit_;

	app::crypto crypto_;

public:
	/**
	 * The convert runnable
	 *
	 * @param inward_channel
	 * @param outward_channel
	 * @param quit
	 */
	convert(migration_data::channel_t& channel_convert, migration_data::channel_t& channel_copy_to_s3, volatile bool& quit) :
			channel_convert_(channel_convert), channel_copy_to_s3_(channel_copy_to_s3), quit_(quit)
	{
	}

	/**
	 * run() thread invoked from here
	 */
	void run()
	{
		try {
			while(!app::quit) {
				migration_data data = channel_convert_.consume();

				const std::string& tmp_prefix(data.identifier+'.');

				try {
					data.p_fstream.reset(new rml::utils::tmp_fstream(rmfs_base_directory+'/'+data.path, std::fstream::in | std::fstream::binary));
					data.clear_size = data.p_fstream->get_size();
					if(0 == data.clear_size)
						throw rml::exception("Zero size file");
				}
				catch(const std::exception& e) {
					LogError("Error opening file; exception:"<<e.what());
					data.state = migration_data::error;
				}

				if(data.state != migration_data::error) {
					rml::utils::tmp_fstream::pointer p_tmp(new rml::utils::tmp_fstream(tmp_prefix, std::size_t(0), temp_files_base_directory,
							std::fstream::in | std::fstream::out | std::fstream::binary | std::fstream::trunc));

					unsigned int iter = 3;
					while(iter && !app::quit) {
						data.p_fstream->seekg(0);

						switch(iter) {

						// compress
						case 3:
						{
							app::bzip2::compress(data.p_fstream, p_tmp, types::file_format::clear_wav, data.identifier);
							data.p_fstream = p_tmp;
							p_tmp.reset(new rml::utils::tmp_fstream(tmp_prefix, std::size_t(0), temp_files_base_directory,
									std::fstream::in | std::fstream::out | std::fstream::binary | std::fstream::trunc));
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
							data.real_size = data.p_fstream->get_size();
							data.p_fstream->close();
							break;
						}

						default:
							break;

						}

						iter--;
					}
				}
				pc_produce(channel_copy_to_s3_, data);
			}
		}
		catch(const migration_data::channel_t::finishing_exception& e) {
			LogVerbose(channel_convert_ << e.what());
			return;
		}
	}
};

}

#endif // CONVERT_HPP
