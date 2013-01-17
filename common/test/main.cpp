#include "../cdr_dir_monitor.hpp"
#if 0
#include "../dir_monitor.hpp"
#else
#include "../inotify_monitor.hpp"
#endif

#include <rml/logger.h>
#include <rml/utils.h>
#include <rml/siginit.h>

#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/ref.hpp>
#include <boost/noncopyable.hpp>

#include <string>
#include <fstream>

#include <unistd.h>

namespace app {
volatile bool quit = false;
volatile bool wasHUPed = false;

void log_boost_error(const boost::system::error_code& e)
{
	LogError(e.message());
}

}

int main(int argc, char* argv[]) {
	try {
		rml::logger::configure();
		rml::siginit();

		boost::thread_group threads;
		boost::asio::io_service io_service;
		boost::shared_ptr<boost::asio::io_service::work> p_work = boost::shared_ptr<boost::asio::io_service::work>(
				new boost::asio::io_service::work(io_service));

		archiving::archivable_busy_set_t archivable_busy_set;
		archiving::archivable_channel_t in_channel(
				boost::bind(&archiving::archivable_busy_set_t::add, boost::ref(archivable_busy_set), _1),
				boost::bind(&archiving::archivable_busy_set_t::is_busied, boost::ref(archivable_busy_set), _1),
				0, "IN");

		archiving::cdr_dir_monitor cdr_dir_monitor(in_channel, "test");

		app::inotify::monitor inotify_monitor(io_service, boost::bind(app::log_boost_error, _1));
		inotify_monitor.add_directory("/mnt/old-home/dkorah/tmp/test-data",
				app::inotify::event::created | app::inotify::event::moved_to | app::inotify::event::closed,
				boost::bind(&archiving::cdr_dir_monitor::handle_event, boost::ref(cdr_dir_monitor), _1, _2));
		inotify_monitor.add_directory("/mnt/old-home/dkorah/tmp/test-data2",
				app::inotify::event::created | app::inotify::event::moved_to | app::inotify::event::closed,
				boost::bind(&archiving::cdr_dir_monitor::handle_event, boost::ref(cdr_dir_monitor), _1, _2));
		inotify_monitor.async_monitor();
		inotify_monitor.async_monitor();

#if 0
		archiving::dir_monitor dir_monitor(io_service);
		dir_monitor.add_directory("/mnt/old-home/dkorah/tmp/test-data",
				archiving::dir_monitor::event::created |
					archiving::dir_monitor::event::moved_to |
					archiving::dir_monitor::event::closed);
		dir_monitor.async_monitor(cdr_dir_monitor);
#endif

//		for(unsigned int i = 0; i < 3; i++)
			threads.create_thread(boost::bind(&boost::asio::io_service::run, boost::ref(io_service)));

		while(!app::quit) {
			try {
				const archiving::archivable::pointer& item(in_channel.consume(1));

				item->get_fstream_ptr()->set_persistent(false);
				archivable_busy_set.remove(item);
			}
			catch(const std::exception& e) {
				LogWarning(e.what());
			}
		}


		in_channel.set_finishing();

		p_work.reset();
		io_service.stop();
		threads.join_all();

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
