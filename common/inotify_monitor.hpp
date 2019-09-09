/*
 * inotify_monitor.hpp
 *
 *  Created on: 17/01/2013
 *	  Author: dkorah
 */

#ifndef INOTIFY_MONITOR_HPP_
#define INOTIFY_MONITOR_HPP_

#include <rml/utils.h>

#include <boost/asio.hpp>
#include <boost/foreach.hpp>
#include <boost/bimap.hpp>
#include <boost/bind.hpp>

#include <string>
#include <sstream>
#include <map>
#include <utility>

#include <sys/inotify.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace app {

namespace inotify {

struct event
{
	enum type_t
	{
		invalid = 0x00000000,

		created = IN_CREATE,
		deleted = IN_DELETE,
		moved_from = IN_MOVED_FROM,
		moved_to = IN_MOVED_TO,
		closed = IN_CLOSE,
		opened = IN_OPEN
	};

	const std::string file_name;
	const std::string dir_name;
	const type_t type;

	friend std::ostream& operator<<(std::ostream& ostr, const event& e)
	{
		ostr << "inotify::event{ dir_name:" << e.dir_name << ", file_name:" << e.file_name << ", type:";

		if(e.type & event::created)
			ostr << "created";
		else if(e.type & event::deleted)
			ostr << "deleted";
		else if(e.type & event::moved_from)
			ostr << "moved-from";
		else if(e.type & event::moved_to)
			ostr << "moved-to";
		else if(e.type & event::closed)
			ostr << "closed";
		else if(e.type & event::opened)
			ostr << "opened";
		else
			ostr << "UNKNOWN-EVENT-TYPE";

		return ostr << " }";
	}

	event(const std::string& file_name, const std::string& dir_name, const type_t& type) :
		file_name(file_name), dir_name(dir_name), type(type)
	{}

	event() : type(invalid)
	{
		/*should never have to use the object constructed by this*/
	}
};

class monitor
{
public:
	typedef boost::function<void (event, boost::system::error_code) > event_handler_t;
	typedef boost::function<void (boost::system::error_code) > error_handler_t;

private:
	enum
	{
		read_buffer_size = 4 * 1024
	};

	typedef std::map< int, event_handler_t > watch_descriptor_handler_map_t;
	typedef boost::bimap< int, std::string > watch_descriptor_path_bimap_t;

public:
	monitor(boost::asio::io_service& io_service, error_handler_t error_handler=error_handler_t())
		: fd_(make_fd()), io_service_(io_service),
		  monitor_stream_descriptor_(io_service, fd_), error_handler_(error_handler),
		  buffer_leftover(0)
	{
	}

	void add_directory(const std::string& dir_name, int event_mask, const event_handler_t& event_handler)
	{
		{
			struct stat s;
			if(stat(dir_name.c_str(), &s) || !(S_ISDIR(s.st_mode)))
				std::invalid_argument(dir_name + " is not a valid directory entry");
		}

		if(0 != (event_mask & ~(
				event::created
				| event::deleted
				| event::moved_from
				| event::moved_to
				| event::opened
				| event::closed)))
			throw (std::invalid_argument("Invalid event mask"));

		int wd = inotify_add_watch(fd_, dir_name.c_str(), event_mask);
		if(wd == -1) {
			std::ostringstream oss;
			oss << "Failed to monitor directory - Directory: " << dir_name << " - Reason:" << strerror(errno);
			throw std::invalid_argument(oss.str());
		}

		watch_descriptors_.insert(watch_descriptor_path_bimap_t::relation(wd, dir_name));
		if(!handlers_.insert(std::make_pair(wd, event_handler)).second)
			THROW_RML_EXCEPTION("Failed to sent event handler for dir:"<<dir_name);
	}

	void remove_directory(const std::string& dir_name)
	{
		watch_descriptor_path_bimap_t::right_iterator it = watch_descriptors_.right.find(dir_name);
		if(it == watch_descriptors_.right.end())
			throw std::invalid_argument("Directory "+dir_name+" is not being monitored");

		handlers_.erase(it->second);

		if(inotify_rm_watch(fd_, it->second) == -1)
			THROW_RML_EXCEPTION("Failed to remove directory monitoration - Directory: " << dir_name << " - Reason:" << strerror(errno));
	}

	void async_monitor()
	{
		async_read();
	}

	~monitor()
	{
		BOOST_FOREACH(watch_descriptor_path_bimap_t::left_reference p, watch_descriptors_.left)
		{
			inotify_rm_watch(fd_, p.first);
		}

		close(fd_);
		fd_ = -1;
	}

private:
	inline static int make_fd()
	{
		int fd = inotify_init();
		if (fd == -1) {
			boost::throw_exception(
					boost::system::system_error(
							boost::system::error_code(errno, boost::system::get_system_category()),
							"init_inotify failed"));
		}

		return fd;
	}

	// Reads data from inotify file descriptor
	void async_read()
	{
		if (buffer_leftover >= read_buffer_size) {
			throw std::runtime_error("no room in inotify read-buffer");
		}

		monitor_stream_descriptor_.async_read_some(
				boost::asio::buffer(read_buffer + buffer_leftover, read_buffer_size - buffer_leftover),
				boost::bind(&monitor::handle_read, this,
							boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
	}

	// Parse events read
	void handle_read(const boost::system::error_code& e, std::size_t bytes_transferred)
	{
		if (e) {
			if (e == boost::asio::error::operation_aborted || fd_ == -1) {
				LogMessage("Closing directory monitoring");
				return;
			}

			if (error_handler_.empty()) {
				LogWarning(e.message());
			} else {
				error_handler_(e);
			}

			return;
		}

		std::size_t bytes_in_buffer = bytes_transferred + buffer_leftover;
		if (bytes_in_buffer < sizeof(inotify_event)) {
			LogVerbose("Found " << bytes_in_buffer << " (" << bytes_transferred << ") bytes in buffer; not enough for inotify_event");

			// read more to process it
			buffer_leftover = bytes_in_buffer;
			async_read();

			return;
		}

		unsigned int bytes_processed = 0;
		while (bytes_in_buffer - bytes_processed >= sizeof(inotify_event)) {
			inotify_event* p_event = reinterpret_cast<inotify_event*> (read_buffer + bytes_processed);

			// take into account the length of `name` buffer (within `inotify_event`) and check again to make sure we
			// have enough bytes in buffer.
			if (bytes_in_buffer - bytes_processed < sizeof(inotify_event) + p_event->len) {
				break;
			}

			watch_descriptor_path_bimap_t::left_iterator it_wdp = watch_descriptors_.left.find(p_event->wd);
			watch_descriptor_handler_map_t::const_iterator it_wdh = handlers_.find(p_event->wd);
			if (it_wdp != watch_descriptors_.left.end() && it_wdh != handlers_.end()) {
				const event ev((p_event->len ? p_event->name : ""), it_wdp->second,
							   static_cast<event::type_t> (p_event->mask));
				it_wdh->second(ev, e);
			} else {
				LogWarning("Invalid / unknown notification from inotify");
			}

			bytes_processed += sizeof(inotify_event) + p_event->len;
		}

		buffer_leftover = bytes_in_buffer - bytes_processed;
		if (buffer_leftover > 0) {
			LogVerbose((bytes_in_buffer - bytes_processed) << " bytes leftover");
			memmove(read_buffer, read_buffer + bytes_processed, buffer_leftover);
		}

		async_read();
	}

	int fd_;

	boost::asio::io_service& io_service_;
	boost::asio::posix::stream_descriptor monitor_stream_descriptor_;

	watch_descriptor_path_bimap_t watch_descriptors_;
	watch_descriptor_handler_map_t handlers_;
	error_handler_t error_handler_;

	char read_buffer[read_buffer_size] __attribute__ ((aligned(__alignof__(inotify_event))));
	std::size_t buffer_leftover;
};

}  // namespace inotify

}  // namespace app

#endif /* INOTIFY_MONITOR_HPP_ */
