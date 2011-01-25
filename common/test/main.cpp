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

#include "../md5.hpp"

namespace app {
bool quit = false;
}

int main(int argc, char* argv[]) {
	try {
		rml::logger::configure();

		rml::siginit();

		for(int i = 0; i < argc; ++i) {
			std::ifstream ifs(argv[i]);
			if(ifs) {
				app::md5hasher::pointer h = app::md5hasher::hash(ifs);
				LogMessage("Hashed: "<<argv[i]<<": "<<*(h.get()));
			}
			else
				LogError("Failed to open file: "<<argv[i]);
		}

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
