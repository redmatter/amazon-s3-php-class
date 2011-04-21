/*
 * file_verify.hpp
 *
 *  Created on: 13 Apr 2011
 *      Author: dkorah
 */

#ifndef FILE_VERIFY_HPP_
#define FILE_VERIFY_HPP_

#ifndef SYSLOG_APP_NAME
#define SYSLOG_APP_NAME "rm-migrationd"
#endif

#include "migration_data.hpp"

#include <rml/pc_channel.h>
#include <rml/utils.h>

#include <boost/date_time/posix_time/ptime.hpp>

#include <ostream>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

namespace app {

extern std::string temp_files_base_directory;

/**
 * file verify runnable
 */
class file_verify
{

public:
	/**
	 * channel data for file-verify
	 */
	struct data
	{
		migration_data md;
		int random;

		/**
		 * Comparator for data
		 */
		struct compare
		{
			/**
			 * () compare!
			 *
			 * @param lhs
			 * @param rhs
			 *
			 * @return bool
			 */
			bool operator()(const data& lhs, const data& rhs)
			{
				return lhs.random < rhs.random;
			}
		};

		typedef rml::pc_channel<data> channel_t;
	};

private:
	data::channel_t& channel_;
	unsigned int check_percentage_;
	std::string log_path_;
	int log_level_;
	volatile bool& quit_;

public:
	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("file_verify.percentage", boost::program_options::value<unsigned int>()->default_value(100U), "verifiy percentatge")
				;
	}

	file_verify(
			const boost::program_options::variables_map& confMap,
			data::channel_t& channel,
			volatile bool& quit) :
				channel_(channel),
				check_percentage_(confMap["file_verify.percentage"].as<unsigned int>()),
				log_path_(confMap["log.file_path"].as<std::string>()),
				log_level_(confMap["log.level"].as<int>()),
				quit_(quit)
	{
	}

	void run()
	{
		uint8_t tick = 10;

		while(!quit_) {

			if(tick--) { // wait for 10s initially and 5s from then on
				boost::this_thread::sleep(boost::posix_time::seconds(1));
				continue;
			}

			tick = 5; // every 5s from now on

			bool abort = false;

			try {
				while(!abort) {
					std::size_t available_items = channel_.count();
					std::size_t todo_count = 1;
					if (available_items > 1)
						todo_count = (size_t)ceilf((float)available_items * ((float)check_percentage_ / 100.0));
					else if(available_items == 0) {
						LogVerbose("No items available to verify");
						break;
					}

					for(std::size_t discard = available_items - todo_count; !quit_ && !abort && discard > 0; discard--)
						(void)channel_.consume(0, 1000);

					while(!quit_ && !abort && todo_count--) {
						LogVerbose("todo_count: "<<todo_count);

						try {
							const data& data(channel_.consume(0, 1000));
							pid_t pid = fork();
							if(pid < 0) {
								LogCritical("Failed to fork; error:"<<strerror(errno));
								LogMessage("Raising Quit flag due to fork failure");
								quit_ = true; // die!!!
								throw rml::exception("Failed to fork");
							}

							if(pid == 0) {
								// we are in the child
								const std::string& source(data.md.p_fstream->get_filename());

								std::string encryption_key_base64;
								{
									const std::vector<uint8_t>& encryption_key(data.md.p_endpoint->encryption_key);
									CryptoPP::StringSource(&encryption_key[0], encryption_key.size(), true,
											new CryptoPP::Base64Encoder(
													new CryptoPP::StringSink(encryption_key_base64), false));
								}

								char * const env[1] = {0};
								const char * argv[16] = {
										"/usr/bin/archiving-convert",
										"--source", &source[0],
										"--verify",
										"--key", &encryption_key_base64[0],
										"--log.file_path", &log_path_[0],
										"--log.level", "6", //TODO use config
										"--log.syslog-tag", SYSLOG_APP_NAME,
										"--temp-dir", &temp_files_base_directory[0],
										0
								};

								execve(argv[0], (char* const*)argv, env);

								LogError("execve failed; error:"<<strerror(errno));
								_exit(rml::utils::exit_codes::fork_exec_failed);
								break;
							}

							else {
								// we are in the parent
								int status = 0;
								int timeout_u_s = 10000000; /*10x1000x1000 micro seconds*/
								bool check_ok = false;
								while(true) {
									pid_t waited_pid = waitpid(pid, &status, WNOHANG);
									if(-1 == waited_pid) {
										if( errno != EINTR ) {
											LogError("waitpid failed: error:"<<strerror(errno));
											break;
										}
										LogError("waitpid: error:"<<strerror(errno));
									}
									else if(0 == waited_pid) {
										LogVerbose("waitpid pid:"<<pid);
										if(timeout_u_s && !quit_) {
											usleep(100);
											timeout_u_s -= 100;
											continue;
										}
										else {
											LogWarning("Convert timed-out; killing pid:"<<pid);
											if(-1 == kill(pid, SIGKILL))
												LogError("Failed to kill pid("<<pid<<") err:"<<strerror(errno));
											break;
										}
									}
									else if( waited_pid == pid ) {
										if( WIFEXITED(status) ) {
											if( 0 == WEXITSTATUS(status) ) {
												check_ok = true;
											}
											break;
										}
									}
									else
										LogMessage("Unexpected: pid:"<<waited_pid<<" finished");
								}

								if(!check_ok) {
									LogCritical(data.md.identifier << " convert failed: file corruption");
									abort = true;
									LogMessage("Raising Quit flag due to file corruption");
									quit_ = true;
									boost::this_thread::sleep(boost::posix_time::milliseconds(100));
									exit(rml::utils::exit_codes::failure);
								}
								else
									LogMessage(data.md.identifier << " verified OK");
							}
						}
						catch(const data::channel_t::consume_timed_out_exception& e) {
							LogMessage(channel_<< ": No file to verify; sleep ...");
						}
						catch(const std::exception& e) {
							LogError("Exception: "<<e.what());
						}
						catch(...) {
							LogError("Unexpected exception");
						}
					}
				}
			}
			catch(const std::exception& e) {
				LogError("Exception: "<<e.what());
			}
			catch(...) {
				LogError("Unexpected exception");
			}
		}
	}
};

}  // namespace app

namespace std {

ostream& operator<< (ostream& ostr, const app::file_verify::data& d)
{
	return ostr << d.md.identifier << " [random:"<<d.random<<']';
}

}  // namespace std

#endif /* FILE_VERIFY_HPP_ */
