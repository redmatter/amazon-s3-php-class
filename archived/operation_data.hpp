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

	static inline_ const char* to_string(const operation_t& op)
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
		error = 2,
		interrupted = 3
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

		init_purge     = 0x07,
		approve_purge  = 0x08,
		file_purge     = 0x09,
		usage_purge    = 0x0a,
		retention_purge = 0x0b,

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

		purge_start            = (operation::delete_object << 8),

		purge_init             = purge_start | (operation_coarse::init_purge << 4) | operation_outcome::unknown,
		purge_init_ok          = purge_init | operation_outcome::ok,
		purge_init_err         = purge_init | operation_outcome::error,

		purge_approve          = purge_start | (operation_coarse::approve_purge << 4) | operation_outcome::unknown,
		purge_approve_ok       = purge_approve | operation_outcome::ok,
		purge_approve_err      = purge_approve | operation_outcome::error,

		purge_file             = purge_start | (operation_coarse::file_purge << 4) | operation_outcome::unknown,
		purge_file_ok          = purge_file | operation_outcome::ok,
		purge_file_err         = purge_file | operation_outcome::error,

		purge_usage            = purge_start | (operation_coarse::usage_purge << 4) | operation_outcome::unknown,
		purge_usage_ok         = purge_usage | operation_outcome::ok,
		purge_usage_err        = purge_usage | operation_outcome::error,

		purge_retention        = purge_start | (operation_coarse::retention_purge << 4) | operation_outcome::unknown,
		purge_retention_ok     = purge_retention | operation_outcome::ok,
		purge_retention_err    = purge_retention | operation_outcome::error,

		// -------------------------

		all_done               = (0x0f << 8) | (operation_coarse::finished << 4) | operation_outcome::ok
	};

	/**
	 * Silly, but helps debugging!
	 */
	static inline_ char const* to_string(const operation_state_t& s)
	{
		switch(s)
		{
		case init:
			return "init";
			break;

		case get_received:
			return "get_received";
			break;

		case get_file_retrieve:
			return "get_file_retrieve";
			break;
		case get_file_retrieve_ok:
			return "get_file_retrieve_ok";
			break;
		case get_file_retrieve_err:
			return "get_file_retrieve_err";
			break;

		case get_file_convert:
			return "get_file_convert";
			break;
		case get_file_convert_ok:
			return "get_file_convert_ok";
			break;
		case get_file_convert_err:
			return "get_file_convert_err";
			break;

		case get_file_send:
			return "get_file_send";
			break;
		case get_file_sent_ok:
			return "get_file_sent_ok";
			break;
		case get_file_sent_err:
			return "get_file_sent_err";
			break;


		// --------------------------


		case put_received:
			return "put_received";
			break;

		case put_file_convert:
			return "put_file_convert";
			break;
		case put_file_convert_ok:
			return "put_file_convert_ok";
			break;
		case put_file_convert_err:
			return "put_file_convert_err";
			break;

		case put_file_save:
			return "put_file_save";
			break;
		case put_file_save_ok:
			return "put_file_save_ok";
			break;
		case put_file_save_err:
			return "put_file_save_err";
			break;

		case put_retention_save:
			return "put_retention_save";
			break;
		case put_retention_save_ok:
			return "put_retention_save_ok";
			break;
		case put_retention_save_err:
			return "put_retention_save_err";
			break;

		case put_usage_save:
			return "put_usage_save";
			break;
		case put_usage_save_ok:
			return "put_usage_save_ok";
			break;
		case put_usage_save_err:
			return "put_usage_save_err";
			break;

		// -------------------------

		case purge_start:
			return "purge_init";
			break;

		case purge_init:
			return "purge_init";
			break;
		case purge_init_ok:
			return "purge_init_ok";
			break;
		case purge_init_err:
			return "purge_init_err";
			break;

		case purge_approve:
			return "purge_approve";
			break;
		case purge_approve_ok:
			return "purge_approve_ok";
			break;
		case purge_approve_err:
			return "purge_approve_err";
			break;

		case purge_file:
			return "purge_file";
			break;
		case purge_file_ok:
			return "purge_file_ok";
			break;
		case purge_file_err:
			return "purge_file_err";
			break;

		case purge_usage:
			return "purge_usage";
			break;
		case purge_usage_ok:
			return "purge_usage_ok";
			break;
		case purge_usage_err:
			return "purge_usage_err";
			break;

		case purge_retention:
			return "purge_retention";
			break;
		case purge_retention_ok:
			return "purge_retention_ok";
			break;
		case purge_retention_err:
			return "purge_retention_err";
			break;

		// -------------------------

		case all_done:
			return "all_done";
			break;
		}

		return "unknown";
	}

	static inline_ operation_state_t make(const operation_state_t& state, const operation_outcome::operation_outcome_t& outcome)
	{
		return operation_state_t((state & 0xff0) | outcome);
	}

	static inline_ bool is_outcome_ok(const operation_state_t& state)
	{
		return operation_outcome::ok == operation_outcome::operation_outcome_t(state & 0x00f);
	}

	static inline_ operation_state_t make(const operation_state_t& state, const bool& outcome_ok)
	{
		return make(state, outcome_ok? operation_outcome::ok: operation_outcome::error);
	}

	static inline_ operation_state_t next_up(const operation_state_t& state)
	{
		if (state == init)
			throw rml::exception("next_up NOT possible from 'init' state");

		if((state & 0x0f0) && (operation_outcome::ok != operation_outcome::operation_outcome_t(state & 0x00f)))
			throw rml::exception("next_up NOT possible if outcome != ok");

		if((state == get_file_sent_ok) || (state == put_usage_save_ok) || (state == purge_retention_ok))
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

		detail() :
			arrival_time(0), identifier(), p_metadata(),
			policy(), p_endpoint(), target_file_format(types::file_format::unknown),
			clear_size(0), real_size(0), current_file_format(types::file_format::unknown),
			files_map(), database_id(0), state(operation_state::init), retry(0)
		{
		}

		detail(const time_t& arrival_time, const archiving::file_type::file_type_t& filetype, const archiving::org_id_t& org,
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
			this->state = operation_state::make(state, operation_outcome::unknown);
			LogVerbose("Loading "<<identifier<<" state:"<<operation_state::to_string(this->state)<<" *original* state:"<<operation_state::to_string(state));
		}

		detail(const archiving::file_type::file_type_t& filetype, const archiving::org_id_t& org, const std::string& uuid) :
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

	typedef boost::function< void (const operation_outcome::operation_outcome_t) > OutcomeHandler;
	OutcomeHandler handler;

	// -------------------- ctor ----------------------
	operation_data()
	{
	}

	operation_data(detail::pointer p_detail) : p_detail(p_detail)
	{
	}

	// -------------------- functions -----------------

	inline_ void hibernate()
	{
		try {
			get_tmp_fstream()->close();
		}
		catch(const std::exception& e) {
			LogWarning(p_detail->identifier<<" Failed to hibernate; err:"<<e.what());
		}
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

			if(itr != end) {
				itr->second->set_persistent(false);
				p_detail->files_map.erase(itr);
			}
			
			rml::utils::tmp_fstream::pointer current = get_tmp_fstream();
			ret = rml::utils::tmp_fstream::create(p_detail->identifier.uuid+"."+types::file_format::to_string(format)+".",
					std::size_t(0), current? current->get_directory() : "/tmp");
			p_detail->files_map.insert(std::make_pair(format, ret));
		}

		return ret;
	}
	
	void cleanup()
	{
		files_map_t::iterator itr = p_detail->files_map.begin();
		while(itr != p_detail->files_map.end()) {
			itr->second->set_persistent(false);
			p_detail->files_map.erase(itr++);
		}
	}

	void remove_tmp_fstream(const types::file_format::file_format_t format, const rml::utils::tmp_fstream::pointer& fs = rml::utils::tmp_fstream::pointer())
	{
		if(p_detail->current_file_format == format)
			throw rml::exception("Removal of current format nor allowed");

		const files_map_t::iterator& end = p_detail->files_map.end();
		files_map_t::iterator itr = p_detail->files_map.find(format);
		if(itr != end) {
			if(fs && (fs.get() != itr->second.get())) {
				LogWarning("removal aborted; requested object did not match");
				return;
			}

			itr->second->set_persistent(false);
			p_detail->files_map.erase(itr);
		}
	}

	struct pointer_compare_lt
	{
		bool operator()(const pointer& lhs, const pointer& rhs) const
		{
			if(lhs->p_detail->retry != rhs->p_detail->retry)
				return lhs->p_detail->retry > rhs->p_detail->retry; // bigger retry, lesser priority ( > comparison )

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

	//typedef rml::pc_channel< operation_data::pointer, operation_data::pointer_compare_lt > channel_t;
	typedef rml::pc_channel_delayed< operation_data::pointer, 0, operation_data::pointer_compare_lt > channel_t;
	typedef rml::busy_set< operation_data::pointer, pointer_compare_dedup_lt > busy_set_t;
};

}  // namespace app

namespace std {

inline_ ostream& operator << (ostream& ostr, app::operation_data::pointer p_opdata)
{
	return ostr << p_opdata->p_detail->identifier << "#state:" << app::operation_state::to_string(p_opdata->p_detail->state);
}

}  // namespace std

#endif /* OPERATION_DATA_HPP_ */
