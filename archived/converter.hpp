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
			database::internal& internal_db,
			volatile bool& quit_flag) :
				convert_channel_(convert_channel),
				convert_done_channel_(convert_done_channel),
				internal_db_(internal_db),
				quit_flag_(quit_flag)
	{
	}

	void run()
	{
		app::impl::crypto crypto;

		while(!quit_flag_) {
			try {
				app::operation_data::pointer opdata = convert_channel_.consume();

				const archiving::archivable::identifier& identifier(opdata->p_detail->identifier);

				opdata->get_tmp_fstream()->re_open();

				bool converted_ok = (opdata->p_detail->current_file_format == opdata->p_detail->target_file_format);
				while(opdata->p_detail->current_file_format != opdata->p_detail->target_file_format) {
					std::pair<app::types::file_format::convert_action::convert_action_t, app::types::file_format::file_format_t> next =
							app::types::file_format::get_next(opdata->p_detail->current_file_format, opdata->p_detail->target_file_format);

					LogDebug(identifier<<": next convert action:"<<app::types::file_format::convert_action::to_string(next.first));

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

						opdata->p_detail->current_file_format = next.second;
					}
					catch(const std::exception& e) {
						LogError(identifier<<": Convert failed; action:"<<app::types::file_format::convert_action::to_string(next.first));
						throw;
					}
				}

				if(converted_ok) {
					opdata->p_detail->state = operation_state::operation_state_t(opdata->p_detail->state + 1);
					opdata->p_detail->real_size = opdata->get_tmp_fstream()->get_size();
					internal_db_.save(opdata); // save before we potentially hand off to another thread
					convert_done_channel_.produce(opdata);
				}
				else {
					opdata->p_detail->retry++;
					if(opdata->p_detail->retry > 3) {
						opdata->p_detail->state = operation_state::operation_state_t(opdata->p_detail->state + 2);
						LogError(identifier<<": Failed convert after "<<opdata->p_detail->retry<<" retry(s)");
						continue;
					}

					internal_db_.save(opdata); // save before we potentially hand off to another thread

					// put it back in; the channel will reduce its priority
					convert_channel_.produce(opdata);
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
