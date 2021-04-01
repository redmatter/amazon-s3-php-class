/*
 * import_dir_monitor.hpp
 *
 *  Created on: 22 sept 2011
 *      Author: jim p
 */

#ifndef DIR_MONITOR_HPP_
#define DIR_MONITOR_HPP_

#include "../common/archivable.hpp"
#include "../common/dir_monitor.hpp"

#include <rml/pc_channel.h>

#include <set>

#include <boost/regex.hpp>
#include <boost/filesystem/operations.hpp>

namespace archiving
{
	class import_dir_monitor
	{
		typedef std::set<std::string> string_set;

		archivable_channel_t& channel_;
		const std::string& local_hostname_;
		string_set files_created_waiting_close_;

	public:
		import_dir_monitor(archivable_channel_t& channel,
				const std::string& local_hostname) :
				channel_(channel), local_hostname_(local_hostname)
		{
		}

		void operator()(dir_monitor::event ev, boost::system::error_code e)
		{
			if (!e)
			{
				LogDebug("import_dir_monitor: received event " << ev);

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
							LogError("Error while processing imported item; " << e.what());
						}
					}
				}
				/* else ignore */
			}
			else
				LogError("import_dir_monitor: err:" << e);
		}

		inline bool add_path(const std::string& file_path)
		{
			static const unsigned int max_delay = 10U;

			try
			{
				static boost::regex uuid_regex("\\/([0-9a-f\\-]{36})[^\\/.]+\\.mp3");

				boost::smatch match;
				if (!boost::regex_search(file_path, match, uuid_regex) || match.size() != 2)
					LogError(std::string("add_path: filename was not in expected format: ") + file_path);
				else {
					// Get the useful bit from the path.
					std::string uuid = match[1].str();

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

					archivable::pointer a(new archivable(uuid, "", 0, file_path, archiving::file_type::recording, "mp3", local_hostname_));
                    a->set_leg_uuid(uuid);

					pc_produce(channel_, a, delay);

					return true;
				}
			}
			catch (const std::exception& e)
			{
				LogError("Error while processing imported item; " << e.what());
			}

			return false;
		}
	};

} // namespace archiving

#endif /* DIR_MONITOR_HPP_ */
