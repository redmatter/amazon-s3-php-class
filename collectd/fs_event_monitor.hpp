/*
 * fs_event_monitor.hpp
 *
 *  Maintainer: dino korah
 *	  Author: Andre Paim (August 22, 2008)
 */

#ifndef FS_EVENT_MONITOR_HPP
#define	FS_EVENT_MONITOR_HPP

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/foreach.hpp>
#include <boost/noncopyable.hpp>

#include "esl.h"

namespace app {

class fs_event_monitor : private boost::noncopyable {
public:

	/// ctor
	fs_event_monitor(esl::ESLconnectionPtr connection, boost::asio::io_service& io_service);

	/// dtor
	~fs_event_monitor();

	/// set filter
	void set_filter(const esl::filters& filters);

	/// read handler type
	typedef boost::function<void (boost::system::error_code, esl::ESLeventPtr, esl::ESLconnectionPtr) > handler_type;

	// Waits for monitored events
	void async_get_events(const handler_type& handler);

private:
	/// Reads data from descriptor
	void async_get_events();

	/// Parse events; hand the control off to ESL to fetch the event
	void handle_event(const boost::system::error_code& e);

	/// the asio scheduler
	boost::asio::io_service& io_service_;

	/// the wrapped ESL (TCP) socket
	boost::asio::ip::tcp::socket socket_;

	/// the handler for read events
	handler_type handler_;

	// the connection
	esl::ESLconnectionPtr connection_;
};

}

#endif	/* FS_EVENT_MONITOR_HPP */
