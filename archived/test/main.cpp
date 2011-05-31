#include <unistd.h>

#include <string>
#include <fstream>

#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/ref.hpp>
#include <boost/noncopyable.hpp>

#include <boost/program_options.hpp>

#include <rml/config.h>
#include <rml/logger.h>
#include <rml/utils.h>
#include <rml/siginit.h>

#include "../crypto.hpp"
#include "../types.hpp"
#include "../operation_data.hpp"
#include "../storage_backend.hpp"
#include "../database.hpp"
#include "../clear_cache.hpp"
#include "../compress.hpp"

namespace app {

volatile bool quit = false;
volatile bool wasHUPed = false;
boost::program_options::variables_map confMap;

std::string operation_data::temp_files_base_directory;

}

int main()
{
	try {

		rml::logger::configure();

		rml::siginit();
		// -----------------------------------------

		boost::program_options::options_description optsDesc;

		app::coreapi::describe_config(optsDesc);
		app::storage::s3::backend::describe_config(optsDesc);
		app::database::internal::describe_config(optsDesc);
		app::database::retention::describe_config(optsDesc);
		app::clear_cache::describe_config(optsDesc);
		app::operation_data::describe_config(optsDesc);

		rml::config::parse("test.conf", optsDesc, app::confMap);

		app::operation_data::temp_files_base_directory = app::confMap["temp_files.base_directory"].as<std::string>();

		app::coreapi coreapi(app::confMap);

		app::database::internal internal_db(app::confMap);
		app::database::retention retention_db(app::confMap);

		app::clear_cache clear_cache(app::confMap, internal_db);

		app::policy::cached_finder_t cached_policy_finder(
				boost::bind(&app::policy::finder, boost::ref(coreapi), _1));
		app::storage::s3::endpoint::cached_finder_t cached_endpoint_finder(
				boost::bind(&app::storage::s3::endpoint::finder, boost::ref(coreapi), _1));

		app::impl::crypto crypto;

		std::istringstream istr;
		istr.str("md5: 52f26204ad24fb35169f79f3a483546d\r\n"
				"CreateTime: 1298851986\r\n"
				"RetentionPolicyID: 1\r\n"
				"LegUUID: 73050cb7-7f1b-422a-8cd9-9a7251a45714\r\n"
				"ResourceID: callrecording/1/59c571ad-bdeb-439a-967d-7ba727ff8ddf.wav\r\n"
				"OrgID: 1\r\n"
				"UserID: 1\r\n"
				"FileType: call-recording\r\n"
				"ContentLength: 864256\r\n"
				"\r\n"
				);

//		app::operation_data::channel_t compress_channel("compress-channel");
//		app::operation_data::channel_t crypto_channel("crypto-channel");

		// recording.wav
		app::operation_data::pointer opdata(new app::operation_data());
		opdata->p_detail.reset(new app::operation_data::detail());
		opdata->p_detail->identifier = "callrecording/1/59c571ad-bdeb-439a-967d-7ba727ff8ddf.wav";
LogDebug("xxxxxxxxxxxxxxxxxxxxxxxxxxxx");
		opdata->p_detail->p_metadata.reset(new app::types::metadata(istr));
		opdata->p_detail->file_type = archiving::archivable::file_type::call_recording;
		opdata->p_detail->org_id = 1;
		opdata->p_detail->policy = app::applied_policy(cached_policy_finder(std::make_pair(opdata->p_detail->org_id, app::policy::id_type(0))), time(0));
		opdata->p_detail->target_file_format = app::types::file_format::make(true, true,
				app::types::data_sub_format::from_filename(opdata->p_detail->identifier));
		opdata->p_detail->p_endpoint = cached_endpoint_finder(1);

		opdata->p_detail->current_file_format = app::types::file_format::clear_wav;
		opdata->p_detail->files_map.insert(std::make_pair(
				app::types::file_format::clear_wav,
				rml::utils::tmp_fstream::pointer(
						new rml::utils::tmp_fstream("/home/dkorah/svn_root/trunk/system/archiving/archived/test/recording.wav",
								std::ios_base::in | std::ios_base::binary))));
		opdata->state = app::operation_state::put_received;

		internal_db.save(opdata);
		clear_cache.put(1, opdata->p_detail->identifier, opdata->p_detail->current_file_format, rml::utils::tmp_fstream::pointer(
				new rml::utils::tmp_fstream("/home/dkorah/svn_root/trunk/system/archiving/archived/test/recording.wav", std::ios_base::in | std::ios_base::binary)));

		rml::utils::tmp_fstream::pointer wav(clear_cache.get(1, opdata->p_detail->identifier, app::types::file_format::clear_wav));
		if(wav)
			LogVerbose(wav->get_filename());

		// ------------ now we have PUT ------------

		while(opdata->p_detail->current_file_format != opdata->p_detail->target_file_format) {

			std::pair<app::types::file_format::convert_action::convert_action_t, app::types::file_format::file_format_t> next =
					app::types::file_format::get_next(opdata->p_detail->current_file_format, opdata->p_detail->target_file_format);

			if(next.second == app::types::file_format::unknown) {
				LogError("Unknown format requested");
				break;
			}

			if(next.first == app::types::file_format::convert_action::nothing) {
				LogVerbose("Done conversions");
				break;
			}

			// lets convert
			switch(next.first) {

			case app::types::file_format::convert_action::compress:
			{
				app::bzip2::compress(opdata, next.second);
				break;
			}
			case app::types::file_format::convert_action::uncompress:
			{
				app::bzip2::decompress(opdata, next.second);
				break;
			}

			case app::types::file_format::convert_action::encrypt:
			{
				crypto.encrypt(opdata, next.second);

				LogMessage("Done PUT ... simulate GET");
				opdata->p_detail->target_file_format = app::types::file_format::clear_wav;
				break;
			}
			case app::types::file_format::convert_action::decrypt:
			{
				crypto.decrypt(opdata, next.second);
				break;
			}

			case app::types::file_format::convert_action::transcode:
			{
				LogMessage("Not implemented!");
				break;
			}

			case app::types::file_format::convert_action::nothing:
				// lets keep the compiler happy!
				break;
			}

			opdata->p_detail->current_file_format = next.second;
		}

		// -----------------------------------------

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

