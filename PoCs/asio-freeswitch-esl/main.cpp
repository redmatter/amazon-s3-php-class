#include <unistd.h>

#include <string>
#include <fstream>

#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/ref.hpp>
#include <boost/noncopyable.hpp>

#include <rml/logger.h>
#include <rml/utils.h>
#include <rml/siginit.h>

#include "esl.h"
#include "fs_event_monitor.hpp"


namespace app {
bool quit = false;
bool wasHUPed = false;
}

class print_event : public boost::noncopyable {
public:
	void print(boost::system::error_code ec, esl::ESLeventPtr ev, esl::ESLconnectionPtr conn) {
		boost::thread::id thread_id = boost::this_thread::get_id();
		ESLconnection* conn_ = conn.get();
		if (ec) {
			LogError(thread_id<<": "<<conn_<<' '<<ec);
			app::quit=true;
		}
		else {
			LogMessage(thread_id<<": "<<conn_<<' '<<ev->serialize("json"));
			LogMessage(thread_id<<": "<<conn_<<" ...event...");
			LogMessage(thread_id<<": "<<conn_<<" connected: "<<conn->connected());
		}
	}
};

int main() {
	try {

		rml::logger::configure();

		rml::siginit();

		esl::ESLconnectionPtr conn(new ESLconnection("localhost", "8021", "ClueCon"));
		if(!conn || !conn->connected())
			throw rml::exception("Could not connect to freeswitch");

		LogMessage("connected!!!");

		conn->send("event plain RE_SCHEDULE HEARTBEAT");

		esl::ESLconnectionPtr conn1(new ESLconnection("localhost", "8021", "ClueCon"));
		if(!conn1 || !conn1->connected())
			throw rml::exception("Could not connect to freeswitch");

		LogMessage("connected!!!");

		conn1->send("event plain RE_SCHEDULE HEARTBEAT");

		boost::asio::io_service ios;
		boost::thread_group tg;

		print_event ep;

		app::fs_event_monitor ev_mon(conn, ios);
		app::fs_event_monitor ev_mon1(conn1, ios);

		tg.create_thread(boost::bind(&boost::asio::io_service::run, boost::ref(ios)));
		tg.create_thread(boost::bind(&boost::asio::io_service::run, boost::ref(ios)));
		tg.create_thread(boost::bind(&boost::asio::io_service::run, boost::ref(ios)));
		tg.create_thread(boost::bind(&boost::asio::io_service::run, boost::ref(ios)));

		// Waits for monitored events
		ev_mon.async_get_events(boost::bind(&print_event::print, &ep, _1, _2, _3));
		ev_mon1.async_get_events(boost::bind(&print_event::print, &ep, _1, _2, _3));

		while(!app::quit && conn->connected()) {
			LogMessage("beep...");
			sleep(1);
		}

		ios.stop();
		tg.join_all();

		LogMessage("Terminated normally");
		exit(0);
	}
	catch(const std::exception& e) {
		LogMessage("Abnormal termination - exception: "<<e.what());
	}
	catch(...) {
		LogMessage("Abnormal termination with unknown exception");
	}

	exit(1);
}
