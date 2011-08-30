/*
 * cdr_dir_monitor.hpp
 *
 *  Created on: 25 Jan 2011
 *      Author: dkorah
 */

#ifndef CDR_DIR_MONITOR_HPP_
#define CDR_DIR_MONITOR_HPP_

#include "archivable.hpp"
#include "dir_monitor.hpp"

#include <rml/pc_channel.h>

#include <set>

namespace archiving {

class cdr_dir_monitor {
	typedef std::set<std::string> string_set;

	archivable_channel_t& channel;
	const std::string& pbx_hostname;
	string_set files_created_waiting_close;

public:
	cdr_dir_monitor(archivable_channel_t& channel, const std::string& pbx_hostname) :
		channel(channel), pbx_hostname(pbx_hostname)
	{
	}

	void operator() (dir_monitor::event ev, boost::system::error_code e)
	{
	    if (!e) {
	    	LogVerbose("cdr_dir_monitor: received event "<<ev);

	    	std::string file_path = ev.dir_name + '/' + ev.file_name;

	        if(ev.type & dir_monitor::event::created) {
				files_created_waiting_close.insert(file_path);
			}
			else if(ev.type & (dir_monitor::event::closed | dir_monitor::event::moved_to)) {
				if( ev.type & dir_monitor::event::moved_to || (files_created_waiting_close.find(file_path) != files_created_waiting_close.end()) ) {
					try {
						std::string uuid(file_path, file_path.length() - (36/*uuid*/+4/*.cdr*/+4/*.xml*/), 36);
						channel.produce(archivable::pointer(new archivable(uuid, "", 0, file_path, archiving::archivable::file_type::cdr, "xml", pbx_hostname)));
						files_created_waiting_close.erase(file_path);
					}
					catch(const std::exception& e) {
						LogError("Error while processing CDR; "<<e.what());
					}
				}
			}
	        /* else ignore */
	    }
	    else
	    	LogError("cdr_dir_monitor: err:"<<e);
	}

	void add_path(const std::string& file_path)
	{
		try {
			std::string uuid(file_path, file_path.length() - (36/*uuid*/+4/*.cdr*/+4/*.xml*/), 36);
			channel.produce(archivable::pointer(new archivable(uuid, "", 0, file_path, archiving::archivable::file_type::cdr, "xml", pbx_hostname)));
		}
		catch(const std::exception& e) {
			LogError("Error while processing CDR; "<<e.what());
		}
	}

};

}  // namespace archiving

#endif /* CDR_DIR_MONITOR_HPP_ */
