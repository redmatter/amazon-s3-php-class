/*
 * directory_loader.h
 *
 *  Created on: 13 Jul 2010
 *      Author: dkorah
 */

#ifndef DIRECTORY_LOADER_H_
#define DIRECTORY_LOADER_H_

#include "globals.h"

#include <rml/exception.h>

#include <dirent.h>
#include <sys/types.h>
#include <errno.h>

namespace archiving
{

class directory_loader
{
	cdr_channel& channel;
	const std::string dir_path;

public:
	directory_loader(cdr_channel& channel, const std::string& dir_path) :
		channel(channel), dir_path(dir_path)
	{
	}

	void run()
	{
		int count = -5;
		while(!app::quit) {

			boost::this_thread::sleep(boost::posix_time::seconds(1));
			if(count++ % 600)
				continue;

			try {
				DIR* dh = opendir(dir_path.c_str());
				if(!dh) {
					LogError("Failed to open directory "<<dir_path<<" - err:"<<strerror(errno));
					throw rml::exception("failed opening directory");
				}

				LogMessage("Loading from directory...");

				struct dirent *dp;
				while((dp = readdir(dh))) {
					if(*(dp->d_name) == '.') // leave hidden files, . and .. alone
						continue;

					struct stat s;
					if (-1 == stat((dir_path+'/'+dp->d_name).c_str(), &s)) {
						LogError("Failed to stat "<<dp->d_name<<" - err: "<<strerror(errno));
						continue;
					}

					if(S_ISDIR(s.st_mode)) // we are not interested in any dirs
						continue;
					else if((time(0) - s.st_ctime) < 3) // lets ignore any freshies for 3secs
						continue;

			        bool isALeg = (0 == strncmp(dp->d_name, "a_", 2));
			        std::string uuid(dp->d_name, ( isALeg? 2 : 0 ), 36);

			        LogMessage("directory_loader: Got " << uuid);

			        //channel.produce(app::cdr_item(uuid, s.st_ctime, isALeg));
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
};

}  // namespace app

#endif /* DIRECTORY_LOADER_H_ */
