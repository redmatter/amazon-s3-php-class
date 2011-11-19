/*
 * converter.hpp
 *
 *  Created on: 24 May 2011
 *      Author: dkorah
 */

#ifndef CONVERTER_HPP_
#define CONVERTER_HPP_

#include "database.hpp"
#include "crypto.hpp"
#include "compress.hpp"

#include <rml/exception.h>

namespace app {

class converter
{
	app::operation_data::channel_t& convert_channel_;
	app::operation_data::channel_t& convert_done_channel_;
	app::operation_data::busy_set_t& operation_data_busy_set_;
	database::internal& internal_db_;
	volatile bool& quit_flag_;

public:
	enum { min_threads = 2 };

	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("converter.threads",  boost::program_options::value<unsigned int>()->default_value(min_threads), "Number of converter threads")
				;
	}

	converter(
			app::operation_data::channel_t& convert_channel,
			app::operation_data::channel_t& convert_done_channel,
			app::operation_data::busy_set_t& operation_data_busy_set,
			database::internal& internal_db,
			volatile bool& quit_flag) :
				convert_channel_(convert_channel),
				convert_done_channel_(convert_done_channel),
				operation_data_busy_set_(operation_data_busy_set),
				internal_db_(internal_db),
				quit_flag_(quit_flag)
	{
	}

	static void cleanup_step(app::operation_data::pointer& opdata, const app::types::file_format::file_format_t& failed_format)
	{
		LogDebug(opdata->p_detail->identifier<<": Sentry cleaning up");
		opdata->get_tmp_fstream()->close();
		opdata->remove_tmp_fstream(failed_format);
	}

	void run()
	{
		app::crypto crypto;

		while(!quit_flag_) {
			try {
				app::operation_data::pointer opdata = convert_channel_.consume();

				if((operation_state::put_file_convert_ok == opdata->p_detail->state) || (((opdata->p_detail->state & 0x00f0) >> 4) > app::operation_coarse::file_convert)) {
					LogVerbose(opdata->p_detail->identifier<<" already converted; shifting to next channel.");
					convert_done_channel_.produce(opdata);
					continue;
				}

				const archiving::archivable::identifier& identifier(opdata->p_detail->identifier);

				opdata->get_tmp_fstream()->re_open();

				bool converted_ok = (opdata->p_detail->current_file_format == opdata->p_detail->target_file_format);
				while(true) {
					if (opdata->p_detail->current_file_format == opdata->p_detail->target_file_format) {
						converted_ok = true;
						LogMessage(identifier<<": No (more) conversion needed; current format:"<<app::types::file_format::to_string(opdata->p_detail->current_file_format));
						break;
					}

					// LogDebug(identifier<<": current format:"<<app::types::file_format::to_string(opdata->p_detail->current_file_format));
					// LogDebug(identifier<<": target format:"<<app::types::file_format::to_string(opdata->p_detail->target_file_format));

					std::pair<app::types::file_format::convert_action::convert_action_t, app::types::file_format::file_format_t> next =
							app::types::file_format::get_next(opdata->p_detail->current_file_format, opdata->p_detail->target_file_format);

					// LogDebug(identifier<<": next convert action:"<<app::types::file_format::convert_action::to_string(next.first));
					// LogDebug(identifier<<": next convert target:"<<app::types::file_format::to_string(next.second));

					try {
						if(next.second == app::types::file_format::unknown) {
							THROW_RML_EXCEPTION("Unknown format requested");
							break;
						}

						if(next.first == app::types::file_format::convert_action::nothing) {
							LogVerbose(identifier<<": Done conversions");
							converted_ok = true;
							break;
						}

						rml::utils::sentry sentry(boost::bind(cleanup_step, opdata, next.second));

						// lets convert
						switch(next.first) {

						case app::types::file_format::convert_action::compress:
							app::bzip2::compress(opdata, next.second);
							break;
						case app::types::file_format::convert_action::uncompress:
							app::bzip2::decompress(opdata, next.second);
							break;

						case app::types::file_format::convert_action::encrypt:
							crypto.encrypt(opdata, next.second);
							break;
						case app::types::file_format::convert_action::decrypt:
							crypto.decrypt(opdata, next.second);
							break;

						case app::types::file_format::convert_action::transcode:
							throw rml::exception("Transcode NOT implemented!");
							break;

						case app::types::file_format::convert_action::nothing:
							// lets keep the compiler happy!
							break;
						}

						LogDebug(identifier<<": current format:"<<app::types::file_format::to_string(opdata->p_detail->current_file_format));
						LogDebug(identifier<<": target format:"<<app::types::file_format::to_string(opdata->p_detail->target_file_format));

						opdata->p_detail->current_file_format = next.second;
						sentry.cancel();
					}
					catch(const std::exception& e) {
						LogError(identifier<<": Convert failed; action:"<<app::types::file_format::convert_action::to_string(next.first)<<" error:"<<e.what());
						break;
					}
				}

				if(converted_ok) {
					opdata->p_detail->retry = 0;
					opdata->p_detail->state = operation_state::make(opdata->p_detail->state, operation_outcome::ok);
					opdata->p_detail->real_size = opdata->get_tmp_fstream()->get_size();
					opdata->get_tmp_fstream()->close();
					
					internal_db_.save(opdata); // save before we potentially hand off to another thread
					convert_done_channel_.produce(opdata);
				}
				else {
					opdata->p_detail->retry++;
					if(opdata->p_detail->retry > 3) {
						opdata->p_detail->state = operation_state::make(opdata->p_detail->state, operation_outcome::error);
						LogError(identifier<<": Failed convert after "<<opdata->p_detail->retry<<" retry(s)");
						continue;
					}
					else
						LogWarning(identifier<<": Failed convert; retrying...");

					internal_db_.save(opdata); // save before we potentially hand off to another thread

					// put it back in; the channel will reduce its priority
					operation_data_busy_set_.remove(opdata);
					convert_channel_.produce(opdata, 5);
				}
			}
			catch(const app::operation_data::channel_t::finishing_exception& e) {
				LogMessage("Channel(s) finishing");
				break;
			}
			catch(const std::exception& e) {
				LogError("exception: "<<e.what());
			}
			catch(...) {
				LogError("Unexpected exception");
			}
		}
	}
};

}  // namespace app

#endif /* CONVERTER_HPP_ */
