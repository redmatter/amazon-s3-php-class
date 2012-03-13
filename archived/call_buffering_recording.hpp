/*
 * call_buffering_recording.hpp
 *
 *  Created on: 27 Feb 2012
 *      Author: dkorah
 */

#ifndef CALL_BUFFERING_RECORDING_HPP_
#define CALL_BUFFERING_RECORDING_HPP_

#include "crypto.hpp"
#include "converter.hpp"

#include <rml/pc_channel_delayed_retry.h>

namespace app {

struct call_buffering_recording
{
	enum {
		retry_minutes = 10
	};

	const archiving::org_id_t org;
	const std::string uuid;
	const std::string leg_uuid;
	const time_t creation_time;

	rml::utils::tmp_fstream::pointer fs;

	unsigned int attempt;

	typedef boost::shared_ptr<call_buffering_recording> pointer;

	call_buffering_recording(
			const archiving::org_id_t& org, const std::string& uuid, const std::string& leg_uuid,
			const time_t& creation_time, const rml::utils::tmp_fstream::pointer& fs) :
				org(org), uuid(uuid), leg_uuid(leg_uuid), creation_time(creation_time), fs(fs),
				attempt(0)
	{
	}

	call_buffering_recording(const call_buffering_recording& cbr) :
		org(cbr.org), uuid(cbr.uuid), leg_uuid(cbr.leg_uuid), creation_time(cbr.creation_time), fs(cbr.fs),
		attempt(cbr.attempt)
	{
	}

	friend std::ostream& operator<<(std::ostream& os, const call_buffering_recording& cbr)
	{
		return os << cbr.org<<'/'<<cbr.uuid<<'@'<<cbr.creation_time;
	}

	// TODO save and restore on restart

	struct pointer_compare
	{
		bool operator()(pointer lhs, pointer rhs)
		{
			if (lhs->creation_time != rhs->creation_time)
				return lhs->creation_time < rhs->creation_time;

			if (lhs->org != rhs->org)
				return lhs->org < rhs->org;

			return lhs->uuid < rhs->uuid;
		}
	};

	struct pointer_get_attempt
	{
		unsigned int operator() (const pointer& i) const
		{
			return i->attempt;
		}
	};

	struct pointer_set_next_attempt
	{
		void operator() (const pointer& i) const
		{
			i->attempt++;
		}
	};

	struct pointer_compute_retry_delay
	{
		unsigned int operator() (const unsigned int& attempt) const
		{
			return attempt * retry_minutes * 60;
		}
	};

	typedef rml::pc_channel_delayed_retry<
			pointer, pointer_get_attempt, pointer_set_next_attempt,
			pointer_compute_retry_delay, pointer_compare, 1 > channel_t;
	typedef rml::busy_set< pointer, pointer_compare > busy_set_t;


	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("call_buffering.base_dir", boost::program_options::value<std::string>()->default_value("/mnt/rmfs/call-buffering"), "Call-buffering base directory on shared filesystem")
				;
	}

	static void run(boost::program_options::variables_map& confMap, storage::cached_endpoint_finder_t& find_endpoint, channel_t& channel, const volatile bool& quit)
	{
		const std::string cb_base_dir(confMap["call_buffering.base_dir"].as<std::string>());
		while(!quit) {
			try {
				pointer p_cbr = channel.consume(10);

				p_cbr->fs->re_open();

				operation_data::detail::pointer detail(new operation_data::detail());
				detail->files_map.insert(std::make_pair(types::file_format::clear_wav, p_cbr->fs));
				detail->identifier = archiving::archivable::identifier(archiving::archivable::file_type::recording, p_cbr->org, p_cbr->uuid);
				detail->p_endpoint = find_endpoint(std::make_pair(p_cbr->org, archiving::endpoint_id_t(0)));
				detail->arrival_time = time(0);
				detail->current_file_format = types::file_format::clear_wav;
				detail->target_file_format = types::file_format::encrypted_compressed_wav;

				detail->state = operation_state::init; // ignored!

				app::operation_data::pointer p_opdata(new operation_data(detail));

				app::crypto crypto;
				app::converter::convert(p_opdata, crypto);

				rml::utils::tmp_fstream::pointer p_ecw = p_opdata->get_tmp_fstream(types::file_format::encrypted_compressed_wav, false, false);
				std::string creation_date_yyyymmdd;
				{
					creation_date_yyyymmdd.reserve(10);
					struct tm * timeinfo = localtime(&p_cbr->creation_time);
					strftime(&creation_date_yyyymmdd[0], 9, "%Y%m%d", timeinfo);
				}

				const std::string& dest_path(boost::str(boost::format("%1%/%2%/%3%/%4%") % cb_base_dir % p_cbr->org % creation_date_yyyymmdd % p_cbr->uuid));
				rml::utils::directory_ready(dest_path);
				if (!p_ecw->safe_move(dest_path, true)) {
					LogError("Saving buffered recording failed");

					p_cbr->fs->set_persistent(true);
					p_cbr->fs->close();

					pc_produce(channel, p_cbr);
				}
				else {
					LogMessage(detail->identifier<<" Buffered call saved OK");
					p_opdata->cleanup();
				}
			}
			catch(std::exception& e)
			{
				LogDError(e.what(), "IGEX");
			}
		}
	}
};

}  // namespace app


namespace std {

ostream& operator<<(ostream& os, const app::call_buffering_recording::pointer& p_cbr)
{
	return os << *p_cbr;
}

}  // namespace std


#endif /* CALL_BUFFERING_RECORDING_HPP_ */
