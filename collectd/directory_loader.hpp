/*
 * directory_loader.hpp
 *
 *  Created on: 30 Aug 2011
 *      Author: dkorah
 */

#ifndef DIRECTORY_LOADER_HPP_
#define DIRECTORY_LOADER_HPP_

#include <rml/exception.h>

#include <boost/program_options.hpp>
#include <boost/bind.hpp>

#include <dirent.h>
#include <sys/types.h>
#include <errno.h>

namespace app {

class directory_loader : public boost::noncopyable {

public:
	typedef boost::function<bool(const std::string&)> loader_fn;
	typedef boost::function<bool(const std::string&)> is_loaded_fn;
	typedef boost::function<bool()> continue_loading_fn;

	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("directory_loader.interval_seconds", boost::program_options::value<unsigned int>()->default_value(min_interval_seconds), "CDR directory loading interval in seconds")
				("directory_loader.batch_size", boost::program_options::value<unsigned int>()->default_value(min_batch_size), "CDR directory loading chunked by this much")
				;
	}

	directory_loader(const std::string& dir_path, const boost::program_options::variables_map& confMap, loader_fn& loader, is_loaded_fn& is_loaded, continue_loading_fn& continue_loading, volatile bool& quit) :
		dir_path_(dir_path), confMap_(confMap), loader_(loader), is_loaded_(is_loaded), continue_loading_(continue_loading), quit_(quit)
	{
	}

	void run()
	{
		int count = -120;

		LogVerbose("Loading from directory in "<<(-1 * count)<<" seconds");

		const unsigned int interval( std::max<unsigned int>(confMap_["directory_loader.interval_seconds"].as<unsigned int>(), min_interval_seconds) );
		const unsigned int batch_size( std::max<unsigned int>(confMap_["directory_loader.batch_size"].as<unsigned int>(), min_batch_size) );

		DIR* dh = 0;

		while(!quit_) {

			boost::this_thread::sleep(boost::posix_time::seconds(1));
			if(count++ % interval)
				continue;

			if(!continue_loading_()) {
				LogMessage("Directory loading paused; waiting for ArchiveDs to come back online");
				continue;
			}

			try {
				// if we are not already within a batch
				if(!dh) {
					dh = opendir(dir_path_.c_str());
					if(!dh) {
						LogError("Failed to open directory "<<dir_path_<<" - err:"<<strerror(errno));
						throw rml::exception("failed opening directory");
					}
					LogMessage("Loading from directory...");
				}
				else
					LogMessage("Continuing loading from directory...");


				struct dirent *dp;
				unsigned int file_count = 0;
				while((file_count < batch_size)) {
					errno = 0;
					dp = readdir(dh);

					if(!dp) {
						if(errno) {
							LogError("Failed to read directory - err:"<<strerror(errno));
							dh = opendir(dir_path_.c_str());
							if(!dh) {
								LogError("Failed to open directory "<<dir_path_<<" - err:"<<strerror(errno));
								throw rml::exception("failed opening directory");
							}
							continue;
						}

						break;
					}

					if(*(dp->d_name) == '.') // leave hidden files, . and .. alone
						continue;

					const std::string file_path(dir_path_+'/'+dp->d_name);

					struct stat s;
					if (-1 == stat(file_path.c_str(), &s)) {
						LogVerbose("Failed to stat "<<file_path<<" - err: "<<strerror(errno));
						continue;
					}

					if(S_ISDIR(s.st_mode)) { // we are not interested in any dirs
						LogDebug("Ignoring directory "<<file_path);
						continue;
					}
					else if((time(0) - s.st_ctime) < 3) // lets ignore any freshies for 3secs
						continue;

					if (!is_loaded_(file_path)) {
						(void)loader_(file_path);
						file_count++;
					}
					else {
						LogDebug("Ignoring invalid item "<<file_path);
					}
				}

				LogVerbose("Loaded "<<file_count<<" files from "<<dir_path_);

				// are we done with the batch ?
				if (dh && !dp) {
					closedir(dh);
					dh = 0;
					dp = 0;

					LogMessage("Loading from directory; done");
				}
			}
			catch(const std::exception& e) {
				LogError(e.what());
			}
		}

		LogMessage("directory_loader terminating...");
	}

private:
	const std::string dir_path_;
	const boost::program_options::variables_map& confMap_;
	loader_fn& loader_;
	is_loaded_fn& is_loaded_;
	continue_loading_fn& continue_loading_;
	volatile bool& quit_;

	enum constant
	{
		min_interval_seconds = 30,
		min_batch_size = 200
	};
};

}  // namespace app

#endif /* DIRECTORY_LOADER_HPP_ */
