/*
 * fs_event_monitor.cpp
 *
 *  Created on: 12 Jul 2010
 *      Author: dkorah
 */

#include <errno.h>
#include <string.h>

#include <string>
#include <utility>

#include <boost/foreach.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "esl.h"

#include "fs_event_monitor.hpp"

namespace app {

/// ctor
fs_event_monitor::fs_event_monitor(esl::ESLconnectionPtr connection, boost::asio::io_service& io_service)
	: io_service_(io_service), socket_(io_service), connection_(connection) {

	// adopt the descriptor
	socket_.assign(boost::asio::ip::tcp::v4(), connection_->socketDescriptor());
	socket_.io_control(boost::asio::ip::tcp::socket::non_blocking(true));
}

/// dtor
fs_event_monitor::~fs_event_monitor() {

}

/// set filter
void fs_event_monitor::set_filter(const esl::filters& filters) {
	std::pair<std::string,std::string> filter;
	BOOST_FOREACH( filter , filters )
	{
		connection_->filter(&filter.first[0], &filter.second[0]);
	}
}

// Waits for monitored events
void fs_event_monitor::async_get_events(const handler_type& handler) {
	handler_ = handler;
	async_get_events();
}

/// Reads data from descriptor
void fs_event_monitor::async_get_events() {
	socket_.async_read_some(boost::asio::null_buffers(),
			boost::bind(&fs_event_monitor::handle_event, this, boost::asio::placeholders::error));
}

/// Parse events; hand the control off to ESL to fetch the event
void fs_event_monitor::handle_event(const boost::system::error_code& e) {

	handler_(e, e? esl::ESLeventPtr(): esl::ESLeventPtr(connection_->recvEvent()), connection_);
}

}  // namespace app
