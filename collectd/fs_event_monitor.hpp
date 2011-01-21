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
	fs_event_monitor(esl::ESLconnectionPtr connection, boost::asio::io_service& io_service)
		: io_service_(io_service), socket_(io_service), connection_(connection) {

		// adopt the descriptor
		socket_.assign(boost::asio::ip::tcp::v4(), connection_->socketDescriptor());

		boost::asio::socket_base::non_blocking_io command(true); 
		socket_.io_control(command);
	}

	/// set filter
	void set_filter(const esl::filters& filters) {
		std::pair<std::string,std::string> filter;
		BOOST_FOREACH( filter , filters )
		{
			connection_->filter(&filter.first[0], &filter.second[0]);
		}
	}

	/// read handler type
	typedef boost::function<void (boost::system::error_code, esl::ESLeventPtr, esl::ESLconnectionPtr) > handler_type;

	// Waits for monitored events
	void async_get_events(const handler_type& handler) {
		handler_ = handler;
		connection_->send("event plain ALL");
		async_get_events();
	}

private:
	fs_event_monitor();

	/// Reads data from descriptor
	void async_get_events() {
		socket_.async_read_some(boost::asio::null_buffers(),
				boost::bind(&fs_event_monitor::handle_event, this, boost::asio::placeholders::error));
	}

	/// Parse events; hand the control off to ESL to fetch the event
	void handle_event(const boost::system::error_code& e) {

		handler_(e, e? esl::ESLeventPtr(): esl::ESLeventPtr(connection_->recvEvent()), connection_);
	}

private:
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
