/*
 * dir_monitor.hpp
 *
 *  Maintainer: dino korah
 *	  Author: Andre Paim (August 22, 2008)
 */

#ifndef _DIR_MONITOR_HPP
#define	_DIR_MONITOR_HPP

#include <stdexcept>

#include <sys/inotify.h>

#include <boost/asio.hpp>
#include <boost/bimap.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/foreach.hpp>
#include <boost/noncopyable.hpp>

namespace archiving {

class dir_monitor : private boost::noncopyable {
public:

	dir_monitor(boost::asio::io_service& io_service);

	~dir_monitor();

	// Add directory monitoration
	void add_directory(const std::string& dir_name, int event_mask);

	// Remove directory monitoration
	void remove_directory(const std::string& dir_name);

	// Monitor event structure
	struct event {
		enum event_type {
			created = IN_CREATE,
			deleted = IN_DELETE,
			moved_from = IN_MOVED_FROM,
			moved_to = IN_MOVED_TO,
			closed = IN_CLOSE,
			opened = IN_OPEN
		};
		const std::string file_name;
		const std::string dir_name;
		const event_type type;

		event(const std::string& file_name, const std::string& dir_name, const event_type& type) : file_name(file_name), dir_name(dir_name), type(type) {}
		event() : type() {/*should never have to use the object constructed by this*/}
	};

	typedef boost::function<void (event, boost::system::error_code) > handler_type;

	// Waits for monitoration events
	void async_monitor(const handler_type handler);

private:
	// Reads data from inotify file descriptor
	void async_read();

	// Parse events read
	void handle_read(const boost::system::error_code& e, std::size_t bytes_transferred);


	boost::asio::io_service& io_service_;
	bool is_initialized_;
	boost::asio::posix::stream_descriptor monitor_stream_descriptor_;
	int fd_;
	typedef boost::bimap<int, std::string> watch_descriptors_type;
	watch_descriptors_type watch_descriptors_;
	handler_type handler_;
	enum
	{
		max_length = 1024
	};
	unsigned char buffer_[max_length];
};

}

namespace std {

ostream& operator<<(ostream& ostr, const archiving::dir_monitor::event& e);

}  // namespace std

#endif	/* _DIR_MONITOR_HPP */
