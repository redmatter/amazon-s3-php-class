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
#include <boost/unordered_set.hpp>

#include <dirent.h>
#include <sys/types.h>
#include <errno.h>

namespace app
{

class directory_loader
{

public:
	typedef boost::function<bool(const std::string&)> loader_fn;
	typedef boost::function<bool(const std::string&)> is_valid_fn;
	typedef boost::unordered_set<std::string> file_list;

	static void describe_config(
			boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()(
				"directory_loader.interval_seconds",
				boost::program_options::value<unsigned int>()->default_value(
						10U), "SMS directory loading interval in seconds")(
				"directory_loader.files_per_pass",
				boost::program_options::value<unsigned int>()->default_value(
						100U), "Max SMS messages to load per pass");
	}

	directory_loader(const std::string& dir_path,
			const boost::program_options::variables_map& confMap,
			loader_fn& loader, is_valid_fn& is_valid, volatile bool& quit) :
			dir_path_(dir_path), confMap_(confMap), loader_(loader), is_valid_(
					is_valid), quit_(quit)
	{
	}

	void run()
	{
		int count = -120;

		LogVerbose("Loading from directory in " << (-1 * count) << " seconds");

		const unsigned int interval(
				std::max<unsigned int>(
						confMap_["directory_loader.interval_seconds"].as<unsigned int>(),
						min_interval)
		);
		const unsigned int max_items = confMap_["directory_loader.files_per_pass"].as<unsigned int>();

		while (!quit_)
		{
			boost::this_thread::sleep(boost::posix_time::seconds(1));
			if (count++ % interval)
				continue;

			DIR* dh = 0;

			try
			{
				dh = opendir(dir_path_.c_str());
				if (!dh)
				{
					LogError(
							"Failed to open directory " << dir_path_ << " - err:" << strerror(errno));
					throw rml::exception("failed opening directory");
				}

				LogDebug("Loading from directory...");

				int items_added = 0, items_tidied = 0;

				struct dirent *dp;
				while ((dp = readdir(dh)))
				{
					if (*(dp->d_name) != '.') // leave hidden files, . and .. alone
					{
						if (items_added == (int) max_items)
						{
							LogMessage("Processed " << max_items << " messages, leaving the rest for later");
							break;
						}

						const std::string file_path(dir_path_ + '/' + dp->d_name);
						const std::string file_name(dp->d_name);

						struct stat s;
						if (-1 == stat(file_path.c_str(), &s))
						{
							LogError("Failed to stat " << file_path << " - err: " << strerror(errno));
						}
						else
						{
							// we are not interested in any dirs
							// lets ignore any freshies for 3secs
							if (!S_ISDIR(s.st_mode) && ((time(0) - s.st_ctime) > 3))
							{
								if (!in_dir_.insert(file_name).second)
									LogDebug("We have already seen " << file_path);

								if (handled_.find(file_name) != handled_.end())
								{
									LogDebug(file_path << " already handled");
								}
								// this is badly named it should be is_busy_()
								// so a positive return means the file is busy
								else if (is_valid_(file_name))
								{
									LogMessage(file_path << " failed is_valid: we're already working on this file");
								}
								else
								{
									LogMessage("sending " << file_path << " to loader");

									if (!handled_.insert(file_name).second)
										LogError("Error adding " << file_path << " into handled list, calling loader anyway");

									(void) loader_(file_path);
									items_added ++;
								}
							}
						}
					}
				}

				// remove items from handled_ which have been cleared away by the archive daemon
				for (file_list::iterator i = handled_.begin(); i != handled_.end(); )
				{
					if (in_dir_.find(*i) == in_dir_.end())
					{
						// file is in handled_, but not in_dir_, so it's been deleted by the archiving process
						// and can be deleted from our handled_ hash table
						file_list::iterator nexti = i;
						++nexti;
						handled_.erase(i);
						items_tidied ++;
						i = nexti;
					}
					else
						++i;
				}

				LogMessage("directory_loader: " << items_added << " added, " << items_tidied << " tidied");
			}
			catch (const std::exception& e)
			{
				LogError(e.what());
			}

			if (dh)
			{
				closedir(dh);
				dh = 0;
			}
		}

		LogMessage("directory_loader terminating...");
	}

private:

	const std::string dir_path_;
	const boost::program_options::variables_map& confMap_;
	loader_fn& loader_;
	is_valid_fn& is_valid_;
	volatile bool& quit_;
	file_list in_dir_;
	file_list handled_;

	enum constant
	{
		min_interval = 10
	};
};

} // namespace app

#endif /* DIRECTORY_LOADER_HPP_ */
