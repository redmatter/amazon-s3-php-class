/*
 * clear_cache.hpp
 *
 *  Created on: 8 Mar 2011
 *      Author: dkorah
 */

#ifndef CLEAR_CACHE_HPP_
#define CLEAR_CACHE_HPP_

#include <sys/stat.h>
#include <unistd.h>

#include "database.hpp"

namespace app {

class clear_cache
{
	const boost::program_options::variables_map& confMap_;
	internal_database& db_;

public:
	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
			("clear_cache.base_directory", boost::program_options::value<std::string>()->default_value(std::string("/var/lib/archiving/clear_cache")),
					"base directory in which 'clear' files will be stored")
			;
	}

	clear_cache(const boost::program_options::variables_map& confMap, internal_database& db) :
		confMap_(confMap), db_(db)
	{
	}

	/// Get the file from clear cache
	///
	/// @param org_id
	/// @param identifier
	/// @param file_format
	/// @return
	rml::utils::tmp_fstream::pointer get(
			const archiving::org_id org_id,
			const std::string& identifier,
			const types::file_format::file_format_t& file_format)
	{
		LogVerbose("Org:"<<org_id<<'/'<<identifier<<RML_HEX(5, file_format));

		assert(types::data_format::clear == (file_format >> 8));

		try {
			const std::string& path(db_.clear_cache_get_path(org_id, identifier, file_format));
			LogVerbose("Got from DB path:"<<path<<"; opening it...");

			return new rml::utils::tmp_fstream(path, std::ios_base::in | std::ios_base::binary);
		}
		catch(const std::exception& e) {
			LogMessage("Not found in cache ["<<e.what()<<']');
		}

		return rml::utils::tmp_fstream::pointer();
	}

	/// Put / Add an item to clear cache
	///
	/// @param org_id      org ID that the file belongs to
	/// @param identifier  file identifier
	/// @param file_format one of the "clear" formats
	/// @param file        the temporary file that is to be cached
	void put(
			const archiving::org_id org_id,
			const std::string& identifier,
			const types::file_format::file_format_t& file_format,
			rml::utils::tmp_fstream::pointer file)
	{
		LogVerbose("Org:"<<org_id<<'/'<<identifier<<RML_HEX(5, file_format));

		assert(types::data_format::clear == (file_format >> 8));

		try {
			const std::string& cache_path(boost::str(boost::format("%1%/%2%.%3%") % confMap_["clear_cache.base_directory"].as<std::string>() % identifier % file_format));
			LogVerbose("clear cache path: "<<cache_path);

			// try making a hard link into cache folder
			bool is_hardlink = (0 == link(file->get_filename().c_str(), cache_path.c_str()));
			if(!is_hardlink) {
				// if we fail; fall back to copying
				// TODO explore the possibility of reusing the file ... set persistant and use the path
				LogWarning("Hard linking failed; falling back to file copying; err: "<<strerror(errno));
				std::ofstream ofs(cache_path.c_str(), std::ios_base::out | std::ios_base::binary);
				ofs.exceptions(std::ios::badbit | std::ios::failbit);

				file->seekg(0);
				ofs << file->rdbuf();
			}

			db_.clear_cache_put_path(org_id, identifier, file_format, cache_path, is_hardlink);
		}
		catch(const std::exception& e) {
			LogMessage("Failed to add file to clear cache err:"<<e.what());
			// XXX may be we should remove the DB entry / file on disk here depending on the type of excception
		}
	}
};

}  // namespace app

#endif /* CLEAR_CACHE_HPP_ */
