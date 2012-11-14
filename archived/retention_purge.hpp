/*
 * retention_purge.hpp
 *
 *  Created on: 23/08/2012
 *      Author: dkorah
 */

#ifndef RETENTION_PURGE_HPP_
#define RETENTION_PURGE_HPP_

#include "../common/archivable.hpp"
#include "retention_record.hpp"
#include "storage_endpoint.hpp"

#include <mysql++/mysql++.h>

#include <boost/program_options.hpp>

#include <list>

namespace app {

namespace retention_purge {

inline void describe_config(boost::program_options::options_description& optsDesc)
{
	optsDesc.add_options()
			("retention_purge.require_approval", boost::program_options::value<bool>()->default_value(true), "Require approval before real purge")
			("retention_purge.delete_cdr_retention_records", boost::program_options::value<bool>()->default_value(true), "Completely delete CDR records?")
			("retention_purge.db_flush_interval_seconds", boost::program_options::value<unsigned int>()->default_value(30), "purge database flush interval")
			("retention_purge.lag_days", boost::program_options::value<unsigned int>()->default_value(7), "purge date will lag by this many days")
			("retention_purge.approval_run_interval_minutes", boost::program_options::value<unsigned int>()->default_value(1), "approval reload every this many minutes")
			("retention_purge.creation_date_range_start", boost::program_options::value<std::string>()->default_value(""), "purge files created within this range")
			("retention_purge.creation_date_range_end", boost::program_options::value<std::string>()->default_value(""), "purge files created within this range")
			("retention_purge.auto_expiring_filetypes", boost::program_options::value<std::string>()->default_value("cdr"), "factor in endpoint's (S3 only at the moment) auto-expiration capability")
			("retention_purge.enabled_orgs", boost::program_options::value<std::string>()->default_value(""), "purge enabled orgs")
			;
}

inline std::string& get_retention_purge_restrictions(const boost::program_options::variables_map& confMap = boost::program_options::variables_map())
{
	static std::string where;

	if(where.empty()) {
		if(confMap.empty())
			throw rml::exception("confMap empty when initialising!");

		std::string where_enabled_orgs;
		{
			const std::string& enabled_orgs(confMap["retention_purge.enabled_orgs"].as<std::string>());
			if(enabled_orgs.empty()) {
				where = "AND 1 = 0 /* retention_purge.enabled_orgs is empty */";
				return where;
			}

			if(enabled_orgs != "all") {
				if(std::string::npos == enabled_orgs.find(','))
					// not a list
					where_enabled_orgs = "`r`.`OrgID` = "+enabled_orgs;
				else
					// a list or orgs
					where_enabled_orgs = "`r`.`OrgID` IN ( "+enabled_orgs+" )";
			}
		}

		std::string where_creation_date_range;
		{
			const std::string& creation_date_start(confMap["retention_purge.creation_date_range_start"].as<std::string>());
			const std::string& creation_date_end(confMap["retention_purge.creation_date_range_end"].as<std::string>());

			std::string where_start, where_end;

			if (!creation_date_start.empty())
				where_start = std::string("`r`.`CreationDate` >= '")+creation_date_start+"' ";
			if (!creation_date_end.empty())
				where_end = std::string("`r`.`CreationDate` <= '")+creation_date_end+"' ";

			if(!where_start.empty() && !where_end.empty())
				where_creation_date_range = std::string("( ")+where_start+"AND "+where_end+") ";
			else if(!where_start.empty())
				where_creation_date_range = where_start;
			else if(!where_end.empty())
				where_creation_date_range = where_end;
		}

		std::string where_lag_days;
		{
			const unsigned int& lag_days(confMap["retention_purge.lag_days"].as<unsigned int>());
			if(lag_days) {
				std::ostringstream ostr;
				ostr << "`r`.`RetentionMaxDate` < DATE_SUB(CURDATE(), INTERVAL "<<lag_days<<" DAY)";

				where_lag_days = ostr.str();
			}
		}

		if(!where_enabled_orgs.empty())
			where += " AND "+where_enabled_orgs;
		if(!where_creation_date_range.empty())
			where += " AND "+where_creation_date_range;
		if(!where_lag_days.empty())
			where += " AND "+where_lag_days;

		LogDebug("where: "<<where);
	}

	return where;
}

struct state
{
	enum coarse_t
	{
		_invalid_          = 0x00,
		_init_             = 0x01,
		_approve_          = 0x02,
		_delete_file_      = 0x03,
		_delete_usage_     = 0x04,
		_delete_retention_ = 0x05,

		_finished_         = 0x0f
	};

	enum outcome_t
	{
		unknown      = 0x00,
		ok           = 0x01,
		error        = 0x02,
		interrupted  = 0x03
	};

	enum state_t
	{
		invalid                = _invalid_,

		init                   = _init_ << 4,
		init_ok                = init | ok,
		init_error             = init | error,

		approve                = _approve_ << 4,
		approve_ok             = approve | ok,
		approve_error          = approve | error,

		delete_file            = _delete_file_ << 4,
		delete_file_ok         = delete_file | ok,
		delete_file_error      = delete_file | error,

		delete_usage           = _delete_usage_ << 4,
		delete_usage_ok        = delete_usage | ok,
		delete_usage_error     = delete_usage | error,

		delete_retention       = _delete_retention_ << 4,
		delete_retention_ok    = delete_retention | ok,
		delete_retention_error = delete_retention | error,

		finished               = _finished_ << 4,
	};

	static inline char const* to_string(const state_t& s)
	{
		switch(s)
		{
		case init:
			return "init";
			break;
		case init_ok:
			return "init_ok";
			break;
		case init_error:
			return "init_error";
			break;

		case approve:
			return "approve";
			break;
		case approve_ok:
			return "approve_ok";
			break;
		case approve_error:
			return "approve_error";
			break;

		case delete_file:
			return "delete_file";
			break;
		case delete_file_ok:
			return "delete_file_ok";
			break;
		case delete_file_error:
			return "delete_file_error";
			break;

		case delete_usage:
			return "delete_usage";
			break;
		case delete_usage_ok:
			return "delete_usage_ok";
			break;
		case delete_usage_error:
			return "delete_usage_error";
			break;

		case delete_retention:
			return "delete_retention";
			break;
		case delete_retention_ok:
			return "delete_retention_ok";
			break;
		case delete_retention_error:
			return "delete_retention_error";
			break;

		case finished:
			return "finished";
			break;

		case invalid:
		default:
			break;
		}

		return "invalid";
	}

	static inline state_t make(const coarse_t& coarse_state, const outcome_t& outcome = unknown)
	{
		return state_t((coarse_state << 4) | outcome);
	}

	static inline state_t make(const state_t& state, const outcome_t& outcome)
	{
		return state_t((state & 0xff0) | outcome);
	}

	static inline state_t make(const state_t& state, const bool& outcome_ok)
	{
		return make(state, outcome_ok? ok: error);
	}

	static inline coarse_t get_coarse(const state_t& state)
	{
		return coarse_t((state & 0xff0) >> 4);
	}

	static inline bool is_outcome_ok(const state_t& state)
	{
		return ok == outcome_t(state & 0x00f);
	}

	static inline state_t next_up(const state_t& state)
	{
		if (state == invalid)
			throw rml::exception("next_up NOT possible from 'invalid' state");

		if (state == finished)
			throw rml::exception("next_up NOT possible from 'finished' state");

		if(!is_outcome_ok(state))
			throw rml::exception("next_up NOT possible if outcome != ok");

		if(state == delete_retention_ok)
			return finished;

		return make(coarse_t(((state & 0x0f0) >> 4) + 1));
	}
};

struct item
{
	const archiving::archivable::identifier identifier;
	storage::basic_endpoint::pointer endpoint;
	retention_record::pointer retention;
	unsigned long long clear_size;
	unsigned long long real_size;

	uint64_t id;
	state::state_t cur_state;

	mutable volatile bool saved;

	typedef boost::shared_ptr< item > pointer;
	typedef boost::weak_ptr< item > weak_ptr;

	item() : clear_size(0), real_size(0), id(0), cur_state(state::invalid)
	{}

	explicit item(const state::state_t& dummy_state) : clear_size(0), real_size(0), id(0), cur_state(dummy_state)
	{
	}

	explicit item(const archiving::archivable::identifier& identifier,
			const storage::basic_endpoint::pointer& endpoint, const retention_record::pointer& retention_from_db,
			const unsigned long long& clear_size = 0, const unsigned long long& real_size = 0,
			const uint64_t& id = 0, const state::state_t& cur_state = state::init) :
		identifier(identifier), endpoint(endpoint), retention(retention_from_db),
		clear_size(clear_size), real_size(real_size),
		id(id), cur_state(cur_state), saved(true)
	{
	}


	explicit item(const archiving::archivable::identifier& identifier,
			const storage::cached_endpoint_finder_t& endpoint_finder,
			const retention_record::pointer& retention_from_db,
			const unsigned long long& clear_size = 0, const unsigned long long& real_size = 0,
			const uint64_t& id = 0, const state::state_t& cur_state = state::init) :
		identifier(identifier),
		endpoint(endpoint_finder(std::make_pair< archiving::org_id_t, archiving::endpoint_id_t >(
				identifier.org, retention_from_db->endpoint_id))),
		retention(retention_from_db), clear_size(clear_size), real_size(real_size),
		id(id), cur_state(cur_state), saved(true)
	{
	}

	explicit item(const mysqlpp::Row& row, const storage::cached_endpoint_finder_t& endpoint_finder) :
			identifier(archiving::archivable::file_type::file_type_t(rml::utils::strtoull(row[2].c_str())),
					rml::utils::strtoull(row[3].c_str()), row[4].c_str()),
			endpoint(endpoint_finder(std::make_pair< archiving::org_id_t, archiving::endpoint_id_t >(
					identifier.org, rml::utils::strtoull(row[5].c_str())))),
			retention(new retention_record(
					identifier, types::file_format::unknown, 0, rml::utils::strtoull(row[7].c_str()), rml::utils::strtoull(row[8].c_str()),
					boost::gregorian::from_simple_string(row[9].c_str()).year_month_day(),
					boost::gregorian::from_simple_string(row[10].c_str()).year_month_day(),
					boost::gregorian::from_simple_string(row[11].c_str()).year_month_day(),
					0, false, true, endpoint->id, rml::utils::strtoull(row[6].c_str()))),
			clear_size(rml::utils::strtoull(row[12].c_str())),
			real_size(rml::utils::strtoull(row[13].c_str())),
			id(rml::utils::strtoull(row[0].c_str())),
			cur_state(state::state_t(rml::utils::strtoull(row[1].c_str())))
	{
	}
	
	static inline pointer make_from_row(const mysqlpp::Row& row, const storage::cached_endpoint_finder_t& endpoint_finder)
	{
		return pointer(new item(row, endpoint_finder));
	}

	static inline void setup_select(mysqlpp::Query& query, mysqlpp::SQLQueryParms& qparams, const std::string& where = std::string())
	{
		query << "SELECT "
			" ID, "<<state::delete_file<<", FileType, OrgID, " // already approved; might as well use the next state here
			" LCASE(CONCAT(HEX(SUBSTR(UUID, 1, 4)),'-',HEX(SUBSTR(UUID, 5, 2)),'-',HEX(SUBSTR(UUID, 7, 2)),'-',HEX(SUBSTR(UUID, 9, 2)),'-',HEX(SUBSTR(UUID, 11)))) UUID, "
			" EndpointID, RetentionID, PolicyID, PolicyVersion, CreationDate, RetentionMinDate, RetentionMaxDate, "
			" ClearSize, RealSize, HostName "
			"FROM ArchivingInternal.ArchiveDPurgeQueue"
			<< (where.empty()? " WHERE 1 ": " WHERE ") << where;
	}

	static inline void setup_update_by_ids_begin(mysqlpp::Query& query, mysqlpp::SQLQueryParms& qparams, const std::string& update_fields)
	{
		query << "UPDATE ArchivingInternal.ArchiveDPurgeQueue SET "<<update_fields<<" WHERE ID IN(";
	}

	static inline void set_saved(const pointer& p, const bool& is_saved)
	{
		p->saved = is_saved;
	}

	static inline bool is_saved(const pointer& p)
	{
		return p->saved;
	}

	friend inline std::ostream& operator << (std::ostream& ostr, const item& i)
	{
		if(i.cur_state == state::invalid)
			return ostr << "unknown [state:invalid]";

		return ostr << i.identifier<<" [state:"<<state::to_string(i.cur_state)<<']';
	}

	struct extract_id
	{
		inline uint64_t operator()(const pointer& p) const
		{
			return p->id;
		}
	};

	struct extract_endpoint
	{
		inline storage::basic_endpoint::pointer operator()(const pointer& p) const
		{
			return p->endpoint;
		}
	};

	struct extract_identifier
	{
		inline archiving::archivable::identifier operator()(const pointer& p) const
		{
			return p->identifier;
		}
	};

	struct is_filetype_cdr
	{
		inline bool operator()(const pointer& p) const
		{
			return p->identifier.filetype == archiving::archivable::file_type::cdr;
		}

		inline int filetype_as_int() const
		{
			return int(archiving::archivable::file_type::cdr);
		}
	};

	struct extract_retention_id
	{
		inline uint64_t operator()(const pointer& p) const
		{
			return p->retention->id;
		}
	};


	struct pointer_compare_lt
	{
		inline bool operator()(const pointer& lhs, const pointer& rhs) const
		{
			if(lhs->cur_state != rhs->cur_state)
				return lhs->cur_state < rhs->cur_state; // order of states in the enum dictates the order here

			if(lhs->identifier.uuid != rhs->identifier.uuid)
				return lhs->identifier.uuid < rhs->identifier.uuid;

			return lhs->identifier.filetype < rhs->identifier.filetype;
		}
	};


	struct pointer_compare_dedup_lt
	{
		inline bool operator()(const pointer& lhs, const pointer& rhs) const
		{
			if(lhs->identifier.filetype != rhs->identifier.filetype)
				return lhs->identifier.filetype < rhs->identifier.filetype;

			if(lhs->identifier.uuid != rhs->identifier.uuid)
				return lhs->identifier.uuid < rhs->identifier.uuid;

			return lhs->identifier.org < rhs->identifier.org;
		}
	};


	struct pointer_compare_endpoint_lt
	{
		inline bool operator()(const pointer& lhs, const pointer& rhs) const
		{
			return lhs->endpoint->id < rhs->endpoint->id;
		}
	};

	struct pointer_compare_org_lt
	{
		inline bool operator()(const pointer& lhs, const pointer& rhs) const
		{
			return lhs->identifier.org < rhs->identifier.org;
		}
	};

	struct pointer_compare_type_org_endpoint_lt
	{
		inline bool operator()(const pointer& lhs, const pointer& rhs) const
		{
			if(lhs->identifier.org != rhs->identifier.org)
				return lhs->identifier.org < rhs->identifier.org;

			if(lhs->identifier.filetype != rhs->identifier.filetype)
				return lhs->identifier.filetype < rhs->identifier.filetype;

			return lhs->endpoint->id < rhs->endpoint->id;
		}
	};

	typedef rml::pc_channel_delayed< pointer, 0, pointer_compare_lt > channel_t;
	typedef rml::busy_set< pointer, pointer_compare_dedup_lt > busy_set_t;
	typedef std::set< weak_ptr > weak_set_t;
	typedef std::set< pointer, pointer_compare_dedup_lt > set_t;
	typedef std::multiset< pointer, pointer_compare_endpoint_lt > endpoint_grouped_multiset_t;
	typedef std::multiset< pointer, pointer_compare_type_org_endpoint_lt > type_org_endpoint_grouped_multiset_t;
	typedef std::multiset< pointer, pointer_compare_org_lt > org_grouped_multiset_t;

	typedef std::list< pointer > list_t;

	static weak_set_t save_db_set;
	static boost::mutex save_db_mutex;

	static inline void save_db_begin(mysqlpp::Query& query, mysqlpp::SQLQueryParms& qp)
	{
		query << "INSERT INTO `ArchivingInternal`.`ArchiveDPurgeQueue` "
				"("
				"`State`, "
				"`FileType`, `OrgID`, `UUID`, "
				"`EndpointID`, "
				"`RetentionID`, `PolicyID`, `PolicyVersion`, `CreationDate`, `RetentionMinDate`, `RetentionMaxDate`, "
				"`ClearSize`, `RealSize`, "
				"`HostName`"
				") "
				"VALUES ";
	}

	static inline void save_db(const pointer& p, mysqlpp::Query& query, mysqlpp::SQLQueryParms& qp, const std::string& hostname)
	{
		query << '('
				<< static_cast<int>(p->cur_state)<<','

				<< static_cast<int>(p->identifier.filetype)<<", "
				<< static_cast<int64_t>(p->identifier.org)<<", "
				<< "UNHEX(REPLACE( '"<<p->identifier.uuid<<"', '-', '' )), "

				<< static_cast<int64_t>(p->retention->endpoint_id)<<", "

				<< static_cast<int64_t>(p->retention->id)<<", "
				<< static_cast<int64_t>(p->retention->policy_id)<<", "
				<< static_cast<int>(p->retention->policy_version)<<", "
				<<'\''<<applied_policy::ymd_t::year_type::value_type(p->retention->creation_date.year)<<'-'<<applied_policy::ymd_t::month_type::value_type(p->retention->creation_date.month)<<'-'<<applied_policy::ymd_t::day_type::value_type(p->retention->creation_date.day)<<"', "
				<<'\''<<applied_policy::ymd_t::year_type::value_type(p->retention->retention_min_date.year)<<'-'<<applied_policy::ymd_t::month_type::value_type(p->retention->retention_min_date.month)<<'-'<<applied_policy::ymd_t::day_type::value_type(p->retention->retention_min_date.day)<<"', "
				<<'\''<<applied_policy::ymd_t::year_type::value_type(p->retention->retention_max_date.year)<<'-'<<applied_policy::ymd_t::month_type::value_type(p->retention->retention_max_date.month)<<'-'<<applied_policy::ymd_t::day_type::value_type(p->retention->retention_max_date.day)<<"', "

				<< static_cast<int64_t>(p->clear_size)<<", "
				<< static_cast<int64_t>(p->real_size)<<", "

				<< '\''<<hostname<<'\''

				<<')';
}

	static inline void save_db_end(mysqlpp::Query& query, mysqlpp::SQLQueryParms& qp)
	{
		query << " ON DUPLICATE KEY UPDATE `State`=VALUES(`State`)";
	}
};

}  // namespace retention_purge

}  // namespace app

namespace std {

inline ostream& operator << (ostream& ostr, const app::retention_purge::item::pointer& p)
{
	return ostr << *p;
}

}  // namespace std


#endif /* RETENTION_PURGE_HPP_ */
