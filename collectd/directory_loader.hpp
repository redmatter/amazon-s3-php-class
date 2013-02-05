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

extern const time_t startup_time;

class directory_loader : public boost::noncopyable {

public:
	typedef boost::function<bool(const std::string&)> loader_fn;
	typedef boost::function<bool(const std::string&)> is_loaded_fn;
	typedef boost::function<float()> continue_loading_fn;

	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
                ("directory_loader.interval_seconds", boost::program_options::value<unsigned int>()
                        ->default_value(min_interval_seconds), "Directory loading interval in seconds")
                ("directory_loader.batch_size", boost::program_options::value<unsigned int>()
                        ->default_value(min_batch_size), "Directory loading chunked by this much")
                ("directory_loader.startup_delay_seconds", boost::program_options::value<unsigned int>()
                        ->default_value(120), "Directory loading will wait after a restart")
				;
	}

	directory_loader(const boost::program_options::variables_map& confMap, volatile bool& quit) :
		confMap_(confMap), quit_(quit)
	{
	}

	void run(const std::string name, const std::string& dir_path,
			const loader_fn loader, const is_loaded_fn is_loaded, const continue_loading_fn continue_loading) const
	{
		const unsigned int interval(std::max<unsigned int>(
				confMap_["directory_loader.interval_seconds"].as<unsigned int>(), min_interval_seconds) );
		const unsigned int batch_size(std::max<unsigned int>(
				confMap_["directory_loader.batch_size"].as<unsigned int>(), min_batch_size) );
		const unsigned int startup_delay_seconds(std::max<unsigned int>(
				confMap_["directory_loader.startup_delay_seconds"].as<unsigned int>(), min_startup_delay_seconds) );

		int count(-startup_delay_seconds);
		LogVerbose(name<<": ""Loading from directory in "<<(-1 * count)<<" seconds");

		while(!quit_) {
			
			boost::this_thread::sleep(boost::posix_time::seconds(1));
			if((count++ % interval) || (count < 0))
				continue;

			const float& factored_batch_size((float)batch_size * continue_loading());
			if(!factored_batch_size) {
				LogMessage(name<<": ""Directory loading paused; waiting for ArchiveDs to come back online ["<<continue_loading()<<']');
				continue;
			}

			try {
				LogMessage(name<<": ""Loading from directory "<<dir_path);
				DIR* dh = opendir(dir_path.c_str());
				if(!dh)
					THROW_RML_EXCEPTION("failed opening directory err:"<<strerror(errno));
				
				rml::utils::sentry closedir_sentry(boost::bind(::closedir, dh));
				
				LogMessage(name<<": ""Loading from directory...");

				struct dirent *dp;
				unsigned int file_count = 0;
				while((file_count < factored_batch_size)) {
					errno = 0;
					dp = readdir(dh);

					if(!dp) {
						if(errno) {
							LogError(name<<": ""Failed to read directory - err:"<<strerror(errno));
							continue;
						}

						break;
					}

					if(*(dp->d_name) == '.') // leave hidden files, . and .. alone
						continue;

					const std::string file_path(dir_path+'/'+dp->d_name);

					struct stat s;
					if (-1 == stat(file_path.c_str(), &s)) {
						LogMessage(name<<": ""Failed to stat "<<file_path<<" - err: "<<strerror(errno)<<" might be already processed");
						continue;
					}

					if(S_ISDIR(s.st_mode)) { // we are not interested in any dirs
						LogDebug(name<<": ""Ignoring directory "<<file_path);
						continue;
					}
					else {
						const time_t age_seconds(time(0) - s.st_ctime);
						if ((s.st_ctime > startup_time) && age_seconds < 1800) {
							if(age_seconds > 60)
								LogDebug(name<<": ""WARNING: "<<file_path<<" not inspected for "<<age_seconds<<" seconds");
							
							continue;
						}
					}

					if (!is_loaded(file_path)) {
						
						(void)loader(file_path);
						LogDebug(name<<": "<<file_path<<" loaded.");
						file_count++;
					}
					else {
						LogDebug(name<<": ""Ignoring invalid item "<<file_path);
					}
				}

				LogVerbose(name<<": ""Loaded "<<file_count<<" files from "<<dir_path);
				LogMessage(name<<": ""Loading from directory; done");
			}
			catch(const std::exception& e) {
				LogError(e.what());
			}

			count = 1;
		}

		LogMessage(name<<": ""directory_loader terminating...");
	}

private:
	const boost::program_options::variables_map& confMap_;
	volatile bool& quit_;

	enum constant
	{
		min_interval_seconds = 10,
		min_batch_size = 100,
		min_startup_delay_seconds = 10
	};
};

}  // namespace app

#endif /* DIRECTORY_LOADER_HPP_ */
