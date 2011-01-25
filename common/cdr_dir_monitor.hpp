/*
 * cdr_dir_monitor.hpp
 *
 *  Created on: 25 Jan 2011
 *      Author: dkorah
 */

#ifndef CDR_DIR_MONITOR_HPP_
#define CDR_DIR_MONITOR_HPP_

#include <set>

#include <rml/pc_channel.h>

#include "archived_file.hpp"

namespace archiving {

class cdr_dir_monitor {
	typedef std::set<std::string> string_set;

	archived_file_channel_t& channel;
	string_set files_created_waiting_close;

public:
	cdr_dir_monitor(archived_file_channel_t& channel) : channel(channel) { }

	void operator() (dir_monitor::event ev, boost::system::error_code e) {
	    if (!e) {
	    	LogVerbose("cdr_dir_monitor: received event "<<ev);

	    	std::string file_path = ev.dir_name + '/' + ev.file_name;

	        if(ev.type & dir_monitor::event::created) {
				files_created_waiting_close.insert(file_path);
			}
			else if(ev.type & (dir_monitor::event::closed | dir_monitor::event::moved_to)) {
				if( ev.type & dir_monitor::event::moved_to || (files_created_waiting_close.find(file_path) != files_created_waiting_close.end()) ) {
					channel.produce(archived_file::pointer(new archived_file(file_path)));
					files_created_waiting_close.erase(archived_file);
				}
			}
	        /* else ignore */
	    }
	    else
	    	LogError("cdr_dir_monitor: err:"<<e);
	}
};

}  // namespace archiving

#endif /* CDR_DIR_MONITOR_HPP_ */
