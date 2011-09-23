/*
 * operation_data.hpp
 *
 *  Created on: 21 Feb 2011
 *      Author: dkorah
 */

#ifndef OPERATION_DATA_HPP_
#define OPERATION_DATA_HPP_

#include "types.hpp"
#include "storage_endpoint.hpp"
#include "globals.h"

#include "../common/policy.hpp"
#include "../common/archivable.hpp"

#include <boost/shared_ptr.hpp>

#include <map>
#include <string>

#include <stdint.h>

namespace app {

typedef std::map< types::file_format::file_format_t, rml::utils::tmp_fstream::pointer > files_map_t;

struct operation_state
{
	enum operation_state_t {
		init = 0,

		get_received,

		get_file_retrieve,
		get_file_retrieve_ok,
		get_file_retrieve_err,

		get_file_convert,
		get_file_convert_ok,
		get_file_convert_err,

		get_file_send,
		get_file_sent_ok,
		get_file_sent_err,


		// --------------------------


		put_received, // received PUT and acked OK

		put_file_convert,
		put_file_convert_ok,
		put_file_convert_err,

		put_file_save,
		put_file_save_ok,
		put_file_save_err,

		put_retention_save,
		put_retention_save_ok,
		put_retention_save_err,

		put_usage_save,
		put_usage_save_ok,
		put_usage_save_err,

		// -------------------------

		retention_info_saved
	};
};

struct operation_data
{
	typedef boost::shared_ptr<operation_data> pointer;

	struct detail
	{
		typedef boost::shared_ptr<detail> pointer;

		// the identifier of this object within storage
		archiving::archivable::identifier identifier;

		// The metadata that came in with PUT
		types::metadata::pointer p_metadata;

		// policy applied on this Object
		applied_policy policy;

		// The storage-endpoint definition
		storage::basic_endpoint::pointer p_endpoint;

		// target file format defined according to storage-endpoint
		types::file_format::file_format_t target_file_format;

		// clear file size
		std::size_t clear_size;

		// size of file in final storage format
		std::size_t real_size;

		// state info: current file-format
		types::file_format::file_format_t current_file_format;

		// state info: a map of all intermediate file-formats of this object
		files_map_t files_map;

		// ----------------- state and DB stuff -----------------

		// Database ID
		uint64_t database_id;

		// state info: operation state
		operation_state::operation_state_t state;

		// state info: retry in the current operation state
		uint8_t retry;
	};

	// Pointer to the detail structure
	detail::pointer p_detail;

	// -------------------- statics -----------------

	static std::string temp_files_base_directory;

	// -------------------- functions -----------------

	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
		("temp_files.base_directory", boost::program_options::value<std::string>()->default_value(std::string("/var/lib/archiving/temp")), "base directory in which temp files will be put")
		;
	}

	rml::utils::tmp_fstream::pointer get_tmp_fstream()
	{
		if(p_detail->current_file_format == types::file_format::unknown)
			throw rml::exception("current format is 'unknown'");

		return get_tmp_fstream(p_detail->current_file_format, false, false);
	}

	rml::utils::tmp_fstream::pointer get_tmp_fstream(const types::file_format::file_format_t format,
			const bool create_missing = false, const bool force_new = false)
	{
		rml::utils::tmp_fstream::pointer ret;
		const files_map_t::iterator& end = p_detail->files_map.end();

		files_map_t::iterator itr = p_detail->files_map.find(format);
		if(itr != end)
			ret = itr->second;

		if(itr == end || force_new) {
			if((itr == end) && !create_missing)
				return ret; // empty!

			if(itr != end)
				p_detail->files_map.erase(itr);

			ret.reset(new rml::utils::tmp_fstream(p_detail->identifier, std::size_t(0), temp_files_base_directory));
			p_detail->files_map.insert(std::make_pair(format, ret));
		}

		return ret;
	}

	struct pointer_compare_lt
	{
		bool operator()(const pointer& lhs, const pointer& rhs)
		{
			if(lhs->p_detail->retry != rhs->p_detail->retry)
				return lhs->p_detail->retry > lhs->p_detail->retry; // bigger retry, lesser priority ( > comparison )

			if(lhs->p_detail->state != rhs->p_detail->state)
				return lhs->p_detail->state < rhs->p_detail->state; // order of states in the enum dictates the order here

			if(lhs->p_detail->identifier.uuid != rhs->p_detail->identifier.uuid)
				return lhs->p_detail->identifier.uuid < rhs->p_detail->identifier.uuid;

			return lhs->p_detail->identifier.filetype < rhs->p_detail->identifier.filetype;
		}
	};

	typedef rml::pc_channel< operation_data::pointer, operation_data::pointer_compare_lt > channel_t;
};

}  // namespace app

namespace std {

inline ostream& operator << (ostream& ostr, app::operation_data::pointer p_opdata)
{
	return ostr << p_opdata->p_detail->identifier << "#state:" << p_opdata->p_detail->state;
}

}  // namespace std

#endif /* OPERATION_DATA_HPP_ */
