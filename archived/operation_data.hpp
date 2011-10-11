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

struct operation
{

	// Order of the enum below decides operation queue priority as well; see operation_ex_compare
	//    hence the customer facing get_object set to be the first one; it does expose
	//    a potential DoS, but we should rate-limit such operations before it hits the
	//    ArchiveD
	enum operation_t
	{
		invalid = 0,
		get_object,
		put_object,
		delete_object,
		stat_object,

		n_operation
	};

	static inline const char* to_string(const operation_t& op)
	{
		static const char* str[] = {
				"invalid",
				"get_object",
				"put_object",
				"delete_object",
				"stat_object"
		};

		if(op > invalid && op < n_operation)
			return str[op];

		return str[0];
	}
};

struct operation_outcome
{
	enum operation_outcome_t
	{
		unknown = 0,
		ok = 1,
		error = 2
	};
};

struct operation_coarse
{
	enum operation_coarse_t
	{
		init           = 0x00,
		file_retrieve  = 0x01,
		file_convert   = 0x02,
		file_send      = 0x03,
		file_save      = 0x04,
		retention_save = 0x05,
		usage_save     = 0x06,

		finished       = 0x0f
	};

	enum
	{
		mask = 0x00f0
	};
};

struct operation_state
{
	enum operation_state_t
	{
		init = 0,

		get_received           = (operation::get_object << 8),

		get_file_retrieve      = get_received | (operation_coarse::file_retrieve << 4) | operation_outcome::unknown,
		get_file_retrieve_ok   = get_file_retrieve | operation_outcome::ok,
		get_file_retrieve_err  = get_file_retrieve | operation_outcome::error,

		get_file_convert       = get_received | (operation_coarse::file_convert << 4) | operation_outcome::unknown,
		get_file_convert_ok    = get_file_convert | operation_outcome::ok,
		get_file_convert_err   = get_file_convert | operation_outcome::error,

		get_file_send          = get_received  | (operation_coarse::file_send << 4) | operation_outcome::unknown,
		get_file_sent_ok       = get_file_send | operation_outcome::ok,
		get_file_sent_err      = get_file_send | operation_outcome::error,


		// --------------------------


		put_received           = (operation::put_object << 8), // received PUT and acked OK

		put_file_convert       = put_received | (operation_coarse::file_convert << 4) | operation_outcome::unknown,
		put_file_convert_ok    = put_file_convert | operation_outcome::ok,
		put_file_convert_err   = put_file_convert | operation_outcome::error,

		put_file_save          = put_received | (operation_coarse::file_save << 4) | operation_outcome::unknown,
		put_file_save_ok       = put_file_save | operation_outcome::ok,
		put_file_save_err      = put_file_save | operation_outcome::error,

		put_retention_save     = put_received | (operation_coarse::retention_save << 4) | operation_outcome::unknown,
		put_retention_save_ok  = put_retention_save | operation_outcome::ok,
		put_retention_save_err = put_retention_save | operation_outcome::error,

		put_usage_save         = put_received | (operation_coarse::usage_save << 4) | operation_outcome::unknown,
		put_usage_save_ok      = put_usage_save | operation_outcome::ok,
		put_usage_save_err     = put_usage_save | operation_outcome::error,

		// -------------------------

		all_done               = (0x0f << 8) | (operation_coarse::finished << 4) | operation_outcome::ok
	};

	static inline operation_state_t make(const operation_state_t& state, const operation_outcome::operation_outcome_t& outcome)
	{
		return operation_state_t((state & 0xff0) | outcome);
	}

	static inline operation_state_t make(const operation_state_t& state, const bool& outcome_ok)
	{
		return make(state, outcome_ok? operation_outcome::ok: operation_outcome::error);
	}

	static inline operation_state_t next_up(const operation_state_t& state)
	{
		if (state == init)
			throw rml::exception("next_up NOT possible from 'init' state");

		if((state & 0x0f0) && (operation_outcome::ok != operation_outcome::operation_outcome_t(state & 0x00f)))
			throw rml::exception("next_up NOT possible if outcome != ok");

		if((state == get_file_sent_ok) || (state == put_usage_save_ok))
			return all_done;

		return operation_state_t((state & 0xf00) | (((state & 0x0f0) >> 8) + 1) | operation_outcome::unknown);
	}
};

struct operation_data
{
	typedef boost::shared_ptr<operation_data> pointer;

	struct detail
	{
		typedef boost::shared_ptr<detail> pointer;

		// arrival time
		time_t arrival_time;

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

		detail() {}

		detail(const time_t& arrival_time, const archiving::archivable::file_type::file_type_t& filetype, const archiving::org_id_t& org,
				const std::string& uuid, const std::string& metadata, const policy::pointer& policy,
				const time_t& create_time, const storage::basic_endpoint::pointer& p_endpoint,
				const types::file_format::file_format_t target_file_format, const std::size_t& clear_size,
				const std::size_t& real_size, const types::file_format::file_format_t& current_file_format,
				const files_map_t& files_map, const operation_state::operation_state_t& state,
				const uint64_t& database_id, const uint8_t& retry=0) :
			arrival_time(arrival_time), identifier(filetype, org, uuid), p_metadata(new types::metadata(metadata)),
			policy(policy, create_time), p_endpoint(p_endpoint), target_file_format(target_file_format),
			clear_size(clear_size), real_size(real_size), current_file_format(current_file_format),
			files_map(files_map), database_id(database_id), state(state), retry(retry)
		{
			LogVerbose("Loading "<<identifier);
		}

		detail(const archiving::archivable::file_type::file_type_t& filetype, const archiving::org_id_t& org, const std::string& uuid) :
			arrival_time(0), identifier(filetype, org, uuid), p_metadata(),
			policy(), p_endpoint(), target_file_format(types::file_format::unknown),
			clear_size(0), real_size(0), current_file_format(types::file_format::unknown),
			files_map(), database_id(0), state(operation_state::init), retry(0)
		{
			LogVerbose("Lookup "<<identifier);
		}
	};

	// Pointer to the detail structure
	detail::pointer p_detail;

	// -------------------- ctor ----------------------
	operation_data()
	{
	}

	operation_data(detail::pointer p_detail) : p_detail(p_detail)
	{
	}

	// -------------------- functions -----------------

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

			if(itr != end) {
				itr->second->set_persistent(false);
				p_detail->files_map.erase(itr);
			}

			ret.reset(new rml::utils::tmp_fstream(p_detail->identifier.uuid+"."+types::file_format::to_string(format)+".",
					std::size_t(0), get_tmp_fstream()->get_directory()));
			p_detail->files_map.insert(std::make_pair(format, ret));
		}

		return ret;
	}

	struct pointer_compare_lt
	{
		bool operator()(const pointer& lhs, const pointer& rhs) const
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

	struct pointer_compare_dedup_lt
	{
		bool operator()(const pointer& lhs, const pointer& rhs) const
		{
			if(lhs->p_detail->identifier.filetype != rhs->p_detail->identifier.filetype)
				return lhs->p_detail->identifier.filetype < rhs->p_detail->identifier.filetype;

			if(lhs->p_detail->identifier.uuid != rhs->p_detail->identifier.uuid)
				return lhs->p_detail->identifier.uuid < rhs->p_detail->identifier.uuid;

			return lhs->p_detail->identifier.org < rhs->p_detail->identifier.org;
		}
	};

	typedef rml::pc_channel< operation_data::pointer, operation_data::pointer_compare_lt > channel_t;
	typedef rml::busy_set< operation_data::pointer, pointer_compare_dedup_lt > busy_set_t;
};

}  // namespace app

namespace std {

inline ostream& operator << (ostream& ostr, app::operation_data::pointer p_opdata)
{
	return ostr << p_opdata->p_detail->identifier << "#state:" << p_opdata->p_detail->state;
}

}  // namespace std

#endif /* OPERATION_DATA_HPP_ */
