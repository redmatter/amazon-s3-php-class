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

class directory_loader {

public:
	typedef boost::function<void(const std::string&)> loader_fn;

	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("directory_loader.interval_seconds", boost::program_options::value<unsigned int>()->default_value(60U), "CDR directory loading interval in seconds")
				;
	}

	directory_loader(const std::string& dir_path, const boost::program_options::variables_map& confMap, loader_fn& loader, volatile bool& quit) :
		dir_path_(dir_path), confMap_(confMap), loader_(loader), quit_(quit)
	{
	}

	void run()
	{
		int count = -5;

		const unsigned int interval( std::max<unsigned int>(confMap_["directory_loader.interval_seconds"].as<unsigned int>(), min_interval) );

		while(!quit_) {

			boost::this_thread::sleep(boost::posix_time::seconds(1));
			if(count++ % interval)
				continue;

			try {
				DIR* dh = opendir(dir_path_.c_str());
				if(!dh) {
					LogError("Failed to open directory "<<dir_path_<<" - err:"<<strerror(errno));
					throw rml::exception("failed opening directory");
				}

				LogMessage("Loading from directory...");

				struct dirent *dp;
				while((dp = readdir(dh))) {
					if(*(dp->d_name) == '.') // leave hidden files, . and .. alone
						continue;

					const std::string& file_path(dir_path_+'/'+dp->d_name);

					struct stat s;
					if (-1 == stat(file_path.c_str(), &s)) {
						LogError("Failed to stat "<<file_path<<" - err: "<<strerror(errno));
						continue;
					}

					if(S_ISDIR(s.st_mode)) // we are not interested in any dirs
						continue;
					else if((time(0) - s.st_ctime) < 3) // lets ignore any freshies for 3secs
						continue;

					loader_(file_path);
				}

				closedir(dh);

				LogMessage("Loading from directory; done");
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
	volatile bool& quit_;

	enum constant
	{
		min_interval = 60
	};
};

}  // namespace app

#endif /* DIRECTORY_LOADER_HPP_ */
