/*
 * retention_purge_batch.hpp
 *
 *  Created on: 01/06/2012
 *      Author: dkorah
 */

#ifndef RETENTION_PURGE_BATCH_HPP_
#define RETENTION_PURGE_BATCH_HPP_

#include "monitor.hpp"
#include "database.hpp"
#include "types.hpp"

#include <boost/smart_ptr.hpp>
#include <boost/program_options.hpp>

namespace app {

namespace retention_purge {

void describe_config(boost::program_options::options_description& optsDesc)
{
	optsDesc.add_options()
			("retention_purge.require_approval", boost::program_options::value<bool>()->default_value(true), "Require approval before real purge")
			("retention_purge.db_flush_interval_seconds", boost::program_options::value<unsigned int>()->default_value(30), "purge database flush interval")
			;
}

struct outcome
{
	enum outcome_t
	{
		unknown = 0,
		ok,
		error
	};
};

struct states
{
	enum coarse_t
	{
		_init             = 0x00,

		_approve          = 0x01,
		_delete_file      = 0x02,
		_trim_usage       = 0x03,
		_update_retention = 0x04,

		_finished         = 0x05
	};

	enum states_t
	{
		init                 = (_init << 4) | outcome::unknown,
		init_ok              = init | outcome::ok,
		init_err             = init | outcome::error,

		approve              = (_approve << 4) | outcome::unknown,
		approve_ok           = approve | outcome::ok,
		approve_err          = approve | outcome::error,

		delete_file          = (_delete_file << 4) | outcome::unknown,
		delete_file_ok       = delete_file | outcome::ok,
		delete_file_err      = delete_file | outcome::error,

		trim_usage           = (_trim_usage << 4) | outcome::unknown,
		trim_usage_ok        = trim_usage | outcome::ok,
		trim_usage_err       = trim_usage | outcome::error,

		update_retention     = (_update_retention << 4) | outcome::unknown,
		update_retention_ok  = update_retention | outcome::ok,
		update_retention_err = update_retention | outcome::error,

	};
};

struct item
{
	archiving::archivable::identifier identifier;
	storage::basic_endpoint::pointer p_storage_endpoint;

	size_t real_size;
	policy::id_type id;
	policy::version_type version;
	applied_policy::ymd_t creation_date;
	applied_policy::ymd_t retention_min_date;
	applied_policy::ymd_t retention_max_date;

	/**
	 * Comparator to group them by endpoint
	 */
	struct endpoint_grouped_compare_lt
	{
		bool operator()(const item& l, const item& r)
		{
			return l.p_storage_endpoint->id < r.p_storage_endpoint->id;
		}
	};

	struct compare_lt
	{
		bool operator()(const item& l, const item& r)
		{
			if (l.identifier.filetype != r.identifier.filetype)
				return l.identifier.filetype < r.identifier.filetype;

			if (l.identifier.org != r.identifier.org)
				return l.identifier.org < r.identifier.org;

			return l.identifier.uuid < r.identifier.uuid;
		}
	};
};

typedef boost::unique_ptr< std::set<item, item::compare_lt> > item_set_ptr;
typedef boost::unique_ptr< std::multiset<item, item::endpoint_grouped_compare_lt> > endpoint_grouped_set_ptr;

struct batch
{
	item_set_ptr p_items;
	states::states_t state;
};

}  // namespace retention_purge

}  // namespace app

#endif /* RETENTION_PURGE_BATCH_HPP_ */
