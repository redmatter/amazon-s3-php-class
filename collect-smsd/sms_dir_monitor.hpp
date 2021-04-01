/*
 * sms_dir_monitor.hpp
 *
 *  Created on: 22 sept 2011
 *      Author: jim p
 */

#ifndef SMS_DIR_MONITOR_HPP_
#define SMS_DIR_MONITOR_HPP_

#include "../common/archivable.hpp"
#include "../common/dir_monitor.hpp"

#include <rml/pc_channel.h>

#include <boost/filesystem/operations.hpp>

#include <set>

namespace archiving
{
	class sms_dir_monitor
	{
		typedef std::set<std::string> string_set;

		archivable_channel_t& channel_;
		const std::string& pbx_hostname_;
		string_set files_created_waiting_close_;

	public:
		sms_dir_monitor(archivable_channel_t& channel,
				const std::string& pbx_hostname) :
				channel_(channel), pbx_hostname_(pbx_hostname)
		{
		}

		void operator()(dir_monitor::event ev, boost::system::error_code e)
		{
			if (!e)
			{
				LogDebug("sms_dir_monitor: received event " << ev);

				std::string file_path = ev.dir_name + '/' + ev.file_name;

				if (ev.type & dir_monitor::event::created)
				{
					files_created_waiting_close_.insert(file_path);
				}
				else if (ev.type
						& (dir_monitor::event::closed | dir_monitor::event::moved_to))
				{
					if ((ev.type & dir_monitor::event::moved_to)
							|| (files_created_waiting_close_.find(file_path)
									!= files_created_waiting_close_.end()))
					{
						try
						{
							if (add_path(file_path))
								files_created_waiting_close_.erase(file_path);
						}
						catch (const std::exception& e)
						{
							LogError("Error while processing SMS; " << e.what());
						}
					}
				}
				/* else ignore */
			}
			else
				LogError("sms_dir_monitor: err:" << e);
		}

		inline bool add_path(const std::string& file_path)
		{
			static const unsigned int max_delay = 10U;

			try
			{
				std::string uuid(file_path,
						file_path.length() - (36/*uuid*/+ 4/*.sms*/+ 4/*.xml*/),
						36);
				std::string type(file_path,
						file_path.length() - (3/*[sm]ms*/+ 4/*.xml*/),
						3);

				unsigned int delay = max_delay;

				boost::system::error_code ec;
				std::time_t mod_time = boost::filesystem::last_write_time(boost::filesystem::path(file_path), ec);
				if (ec) {
					LogError(uuid << " - failed to get modification-time; error:" << ec.message());
				} else {
					std::time_t age = std::time(0) - mod_time;
					if (age > 0) {
						delay -= std::max<unsigned int>(max_delay, age);
					}

					if (delay) {
						LogVerbose(uuid << " - delaying by " << delay << " seconds");
					}
				}

				// whether it's treated as an SMS or an MMS depends on the format (xml/email)
				pc_produce(channel_, archivable::pointer(
						new archivable(uuid, "", 0, file_path,
								archiving::file_type::sms, type == "sms" ? "xml" : "email", pbx_hostname_)), delay);

				return true;
			}
			catch (const std::exception& e)
			{
				LogError("Error while processing SMS; " << e.what());
			}

			return false;
		}
	};

} // namespace archiving

#endif /* SMS_DIR_MONITOR_HPP_ */
