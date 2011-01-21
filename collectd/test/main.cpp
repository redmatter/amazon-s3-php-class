#include <signal.h>
#include <unistd.h>

#include <string>
#include <fstream>

#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/ref.hpp>

#include <rml/logger.h>
#include <rml/utils.h>

#include "../esl.h"
#include "../fs_event_monitor.hpp"

namespace app {
bool quit = false;
}

/* save+quit */
void sigint(int s) {
	LogError("SIGINT: Trying a clean exit");
	app::quit = true;
}

/* quit */
void sighup(int s) {
	LogError("SIGHUPed");
}

/* kill */
void sigterm(int s) {
	LogError("SIGTERM: Trying a clean exit");
	app::quit = true;
}

/* save */
void sigusr1(int s) {
	LogError("SIGUSR1 Ignored");
}

/* reset all */
void sigusr2(int s) {
	LogError("SIGUSR2 Ignored");
}

/* sigpipe ?! */
void sigpipe(int s) {
	LogError("SIGPIP Ignored");
}

void siginit(void) {
	signal(SIGINT, (__sighandler_t)sigint );
	signal(SIGHUP, (__sighandler_t)sighup );
	signal(SIGUSR1,(__sighandler_t)sigusr1);
	signal(SIGUSR2,(__sighandler_t)sigusr2);
	signal(SIGPIPE,(__sighandler_t)sigpipe);
	signal(SIGTERM,(__sighandler_t)sigterm);
}

void print_events(boost::system::error_code ec, esl::ESLeventPtr ev, esl::ESLconnectionPtr conn) {
	if (ec) {
		LogError(ec);
	}
	else {
		LogMessage(ev->serialize());
	}
}

int main() {
	try {

		rml::logger::configure();

		siginit();


		boost::asio::io_service ios;
		boost::thread_group tg;

		esl::ESLconnectionPtr conn(new ESLconnection("localhost", "8021", "ClueCon"));

		app::fs_event_monitor ev_mon(conn, ios);

		tg.create_thread(boost::bind(&boost::asio::io_service::run, boost::ref(ios)));
		tg.create_thread(boost::bind(&boost::asio::io_service::run, boost::ref(ios)));
		tg.create_thread(boost::bind(&boost::asio::io_service::run, boost::ref(ios)));
		tg.create_thread(boost::bind(&boost::asio::io_service::run, boost::ref(ios)));

		typedef boost::function<void (boost::system::error_code, esl::ESLeventPtr, esl::ESLconnectionPtr) > handler_type;

		// Waits for monitored events
		void async_get_events(const handler_type& handler) {

		LogMessage("Terminated normally");
		exit(0);
	}
	catch(const std::exception& e) {
		LogMessage("Abnormal termination - exception:"<<e.what());
	}
	catch(...) {
		LogMessage("Abnormal termination with unknown exception");
	}

	exit(1);
}
