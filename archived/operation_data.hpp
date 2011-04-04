/*
 * operation_data.hpp
 *
 *  Created on: 21 Feb 2011
 *      Author: dkorah
 */

#ifndef OPERATION_DATA_HPP_
#define OPERATION_DATA_HPP_

#include <stdint.h>

#include <map>
#include <string>

#include <boost/shared_ptr.hpp>

#include "../common/archivable.hpp"

#include "types.hpp"
#include "storage_endpoint.hpp"
#include "policy.hpp"
#include "globals.h"

namespace app {

typedef std::map< types::file_format::file_format_t, rml::utils::tmp_fstream::pointer > files_map_t;

struct operation_states
{
	enum operation_states_t {
		init = 0,

		get_received,
		get_file_retrieve,
		get_file_convert,
		get_file_send,
		get_file_sent_ok,
		get_file_sent_err,

		put_received,
		put_file_convert,
		put_file_save,
		put_file_save_ok,
		put_file_save_err,
		put_file_save_retry
	};
};

struct operation_data
{
	static std::string temp_files_base_directory;

	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
		("temp_files.base_directory", boost::program_options::value<std::string>()->default_value(std::string("/var/lib/archiving/temp")), "base directory in which temp files will be put")
		;
	}
	
	struct detail
	{
		typedef boost::shared_ptr<detail> pointer;

		std::string identifier; // anything that identifies the object external to the system.
								// proposed to be OrgID/type/UUID.ext
								// This is meant to form part of the URL sent for GET

		types::metadata::pointer p_metadata;
		archiving::archivable::file_type::file_type_t file_type;
		archiving::org_id org_id;
		applied_policy policy;
		types::file_format::file_format_t target_file_format;
		storage::s3::endpoint::pointer p_endpoint;

		// state info
		types::file_format::file_format_t current_file_format;
		files_map_t files_map;

		volatile uint8_t retry;
	};

	typedef boost::shared_ptr<operation_data> pointer;

	uint64_t database_id;

	detail::pointer p_detail;
	volatile operation_states::operation_states_t state;

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
				return lhs->p_detail->retry > lhs->p_detail->retry;

			if(lhs->state != rhs->state)
				return lhs->state < rhs->state;

			return lhs->p_detail->identifier < rhs->p_detail->identifier;
		}
	};

	typedef rml::pc_channel< operation_data::pointer, operation_data::pointer_compare_lt > channel_t;
};

}  // namespace app

#endif /* OPERATION_DATA_HPP_ */
