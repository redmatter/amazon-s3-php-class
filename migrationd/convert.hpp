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
	migration_data::busy_set_t& busied_set_;

	migration_data::channel_t& inward_channel_;
	migration_data::channel_t& outward_channel_;
	volatile bool& quit_;

	app::impl::crypto crypto_;

public:
	/**
	 * The convert runnable
	 *
	 * @param inward_channel
	 * @param outward_channel
	 * @param quit
	 */
	convert(migration_data::busy_set_t& busied_set, migration_data::channel_t& inward_channel, migration_data::channel_t& outward_channel, volatile bool& quit) :
			busied_set_(busied_set), inward_channel_(inward_channel), outward_channel_(outward_channel), quit_(quit)
	{
	}

	/**
	 * run() thread invoked from here
	 */
	void run()
	{
		try {
			while(!app::quit) {
				migration_data data = inward_channel_.consume();

				// busy it here; we will unbusy this once we have done updating Migration.State
				busied_set_.add(data);

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
							break;
						}

						default:
							break;

						}

						iter--;
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

#endif // CONVERT_HPP
