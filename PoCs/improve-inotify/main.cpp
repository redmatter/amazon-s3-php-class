//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 05/09/2019.
//

#define RML_LOGGER_SYSLOG_ONLY 1
#include <rml/logger.hpp>

#include "../../common/inotify_monitor.hpp"

void handler(const app::inotify::event& e, const boost::system::error_code& ec)
{
	std::cout << e << std::endl;
}

int main(int argc, char* argv[])
{
	boost::asio::io_service io_service;

	app::inotify::monitor m(io_service);

	m.add_directory(
			"/var/log/freeswitch/xml_cdr",
			app::inotify::event::created | app::inotify::event::moved_to | app::inotify::event::closed,
			boost::bind(handler, _1, _2));

	m.async_monitor();

	io_service.run();
}
