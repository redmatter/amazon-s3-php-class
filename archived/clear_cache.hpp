/*
 * clear_cache.hpp
 *
 *  Created on: 8 Mar 2011
 *      Author: dkorah
 */

#ifndef CLEAR_CACHE_HPP_
#define CLEAR_CACHE_HPP_

#include "database.hpp"

#include "../common/archivable.hpp"

#include <rml/logger.h>
#include <rml/utils.h>

#include <sys/stat.h>
#include <unistd.h>

namespace app {

extern volatile bool quit;

class clear_cache
{
	const boost::program_options::variables_map& confMap_;
	database::internal& db_;
	std::string base_directory_;

	const bool enabled_;

public:
	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("clear_cache.enabled", boost::program_options::value<std::string>()->default_value(std::string("false")), "Clear cache enabled?")
				("clear_cache.base_directory", boost::program_options::value<std::string>()->default_value(std::string("")), "base directory in which 'clear' files will be stored")
			;
	}

	clear_cache(const boost::program_options::variables_map& confMap, database::internal& db) :
		confMap_(confMap), db_(db),
		base_directory_(confMap_["clear_cache.base_directory"].as<std::string>()),
		enabled_(confMap_["clear_cache.enabled"].as<std::string>() == "true")
	{
		// ctor
	}

	/// Get the file from clear cache
	/// @param org_id
	/// @param identifier
	/// @param file_format
	/// @return
	rml::utils::tmp_fstream::pointer get(
			const archiving::archivable::identifier& identifier,
			const types::file_format::file_format_t& file_format)
	{
		if(!enabled_) {
			LogDebug("clear-cache is disabled");
			return rml::utils::tmp_fstream::pointer();
		}

		LogDebug(identifier<<": Get file_format:"<<RML_HEX(5, file_format));

		assert(types::data_format::clear == (file_format >> 8));

		try {
			const std::string& path(db_.clear_cache_get_path(identifier.org, identifier.uuid, file_format));
			LogDebug(identifier<<": Got from DB path:"<<path<<"; opening it...");

			return rml::utils::tmp_fstream::pointer(new rml::utils::tmp_fstream(path, std::ios_base::in | std::ios_base::binary));
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
			const archiving::archivable::identifier& identifier,
			const types::file_format::file_format_t& file_format,
			rml::utils::tmp_fstream::pointer file)
	{
		if(!enabled_) {
			LogDebug("clear-cache is disabled");
			return;
		}

		LogDebug(identifier<<": Put file_format:"<<RML_HEX(5, file_format));

		assert(types::data_format::clear == (file_format >> 8));

		try {
			const std::string& cache_path(
						boost::str(
							boost::format("%1%/%2%.%3%") % base_directory_ % archiving::archivable::identifier::to_string(identifier) % types::file_format::to_extension(file_format)));

			LogDebug(identifier<<": clear cache path: "<<cache_path);

			bool is_linked = false;
			bool path_exists = false;
			bool valid_link = false;
			struct stat s;
			if((0 != stat(cache_path.c_str(), &s)) && (errno != ENOENT))
				THROW_RML_EXCEPTION("stat() failed: error:"<<strerror(errno));

			is_linked = s.st_nlink > 1;
			path_exists = (errno != ENOENT);
			if(is_linked) {
				struct stat ss;
				if(0 == stat(file->get_filename().c_str(), &ss))
					valid_link = s.st_ino == ss.st_ino;
				else
					THROW_RML_EXCEPTION("stat() failed: error:"<<strerror(errno));
			}

			if(path_exists && ( !is_linked || !valid_link)) {
				path_exists = 0 != unlink(cache_path.c_str());
				if(path_exists) {
					THROW_RML_EXCEPTION("unlink() failed: error:"<<strerror(errno));
				}
			}

			bool is_hardlink = (path_exists && is_linked && valid_link);

			if(!path_exists) {
				rml::utils::directory_ready(cache_path);

				// try making a hard link into cache folder
				is_hardlink = (0 == link(file->get_filename().c_str(), cache_path.c_str()));
				if(!is_hardlink) {
					// if we fail; fall back to copying
					// TODO explore the possibility of reusing the file ... set persistant and use the path
					LogWarning("Hard linking failed; falling back to file copying; err: "<<strerror(errno));
					std::ofstream ofs(cache_path.c_str(), std::ios_base::out | std::ios_base::binary);
					ofs.exceptions(std::ios::badbit | std::ios::failbit);

					file->seekg(0);
					std::copy(std::istream_iterator<unsigned char>(*file), std::istream_iterator<unsigned char>(), std::ostream_iterator<unsigned char>(ofs, ""));
				}
			}

			int retry = 0;
			while(!db_.clear_cache_put_path(identifier.org, identifier.uuid, file_format, cache_path, is_hardlink) && !quit) {
				LogMessage("[retry: "<<(++retry)<<"] clear_cache_put_path failed; retrying...");
				boost::this_thread::sleep(boost::posix_time::seconds(15));
			}
		}
		catch(const std::exception& e) {
			LogWarning("Failed to add file to clear cache err:"<<e.what());
			// XXX may be we should remove the DB entry / file on disk here depending on the type of exception
		}
	}
};

}  // namespace app

#endif /* CLEAR_CACHE_HPP_ */
