/*
 * cdr_dir_monitor.hpp
 *
 *  Created on: 25 Jan 2011
 *      Author: dkorah
 */

#ifndef CDR_DIR_MONITOR_HPP_
#define CDR_DIR_MONITOR_HPP_

#include "../common/archivable.hpp"
#if 0
#include "../common/dir_monitor.hpp"
#else
#include "../common/inotify_monitor.hpp"
#endif
#include <rml/pc_channel.h>

#include <boost/noncopyable.hpp>

#include <set>

namespace archiving {

class cdr_dir_monitor : private boost::noncopyable
{
	typedef std::set<std::string> string_set;

	archivable_channel_t& channel_;
	const std::string& pbx_hostname_;
	string_set files_created_waiting_close_;

	boost::mutex mutex_;

public:
	cdr_dir_monitor(archivable_channel_t& channel, const std::string& pbx_hostname) :
		channel_(channel), pbx_hostname_(pbx_hostname)
	{
	}


#if 0
	void handle_event(dir_monitor::event ev, boost::system::error_code e)
	{
	    if (!e) {
	    	LogDebug("cdr_dir_monitor: received event "<<ev);

	    	std::string file_path = ev.dir_name + '/' + ev.file_name;

	        if(ev.type & dir_monitor::event::created) {
				files_created_waiting_close_.insert(file_path);
			}
			else if(ev.type & (dir_monitor::event::closed | dir_monitor::event::moved_to)) {
				if( ev.type & dir_monitor::event::moved_to || (files_created_waiting_close_.find(file_path) != files_created_waiting_close_.end()) ) {
					try {
						if(add_path(file_path))
							files_created_waiting_close_.erase(file_path);
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
#endif

	void handle_event(app::inotify::event ev, boost::system::error_code e,
			const size_t rev_path_uuid_idx, const archiving::file_type::file_type_t& file_type, const char* data_format)
	{
	    if (!e) {
	    	LogDebug("cdr_dir_monitor: received event "<<ev);

	    	boost::unique_lock<boost::mutex> lock(mutex_);

	    	std::string file_path = ev.dir_name + '/' + ev.file_name;

			// If the file has just been created (i.e. not closed yet) just add it to the set of files
			// created which are awaiting closure.
	        if(ev.type & app::inotify::event::created) {
				files_created_waiting_close_.insert(file_path);
			}
			// Otherwise, check if it's just been closed or has just been moved to the directory we're monitoring.
			else if(ev.type & (app::inotify::event::closed | app::inotify::event::moved_to)) {
				// If it was just closed, we should be able to find it in our set of files which were created and are
				// awaiting closure.  Alternatively, it could have been moved to our directory.  Check that one of these
				// conditions has been met.
				if( (ev.type & app::inotify::event::moved_to) || (files_created_waiting_close_.find(file_path) != files_created_waiting_close_.end()) ) {
					try {
						add_path(file_path, rev_path_uuid_idx, file_type, data_format);
						files_created_waiting_close_.erase(file_path);
					}
					catch(const std::exception& e) {
						LogError("Error while processing CDR; "<<e.what());
					}
				}
				// We would get here if a file has been closed but it wasn't in our list of open files.
				// We don't expect to get here, but if we do, there's no special action we could take.
			}
	        /* else ignore */
	    }
	    else
	    	LogError("cdr_dir_monitor: err:"<<e);
	}

	// Adds the passed path to the channel.
	inline bool add_path(const std::string& file_path,
			const size_t rev_path_uuid_idx, const archiving::file_type::file_type_t& file_type, const char* data_format)
	{
		try {
			// The UUID can be extracted from the file path, starting from the passed index and continuing for 36
			// characters, the length of the uuid.
			std::string uuid(file_path, file_path.length() - rev_path_uuid_idx, 36);
			archivable::pointer p_archivable(new archivable(uuid, "", 0, file_path, file_type, data_format, pbx_hostname_));
			p_archivable->hibernate();

			pc_produce(channel_, p_archivable);

			return true;
		}
		catch(const std::exception& e) {
			LogError("Error while processing CDR; "<<e.what());
		}

		return false;
	}

};

}  // namespace archiving

#endif /* CDR_DIR_MONITOR_HPP_ */
