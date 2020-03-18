//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 09 February 2020.
//

#ifndef ARCHIVING_COMMON_ARCHIVED_STATS_SNAPSHOT_HPP
#define ARCHIVING_COMMON_ARCHIVED_STATS_SNAPSHOT_HPP

#include "../../../external/runstats/regression.hpp"
#include "item.hpp"

#include <algorithm>
#include <boost/chrono/duration.hpp>
#include <cstddef>
#include <cstring>

namespace {

inline double
get_processing_count(const common::archived::stats::item_t& i)
{
	return double(i.convert_waiting + i.convert_done_waiting + i.storage_ops_waiting + i.retention_store_waiting);
}

}

namespace common {
namespace archived {
namespace stats {

struct trend
{
	enum type_t
	{
		unchanged = 0,
		up,
		down
	};

	type_t value;
	double slope;

	std::time_t time;
	std::size_t convert_waiting;
	std::size_t convert_done_waiting;
	std::size_t storage_ops_waiting;
	std::size_t retention_store_waiting;

	static const char* value_str(type_t value)
	{
		switch (value) {
			case unchanged:
				return "unchanged";
			case up:
				return "up";
			case down:
				return "down";
		}

		return "unknown";
	}

	static type_t from_slope(double slope)
	{
		if (slope < 0) {
			return trend::down;
		}

		if (slope > 0) {
			return trend::up;
		}

		return trend::unchanged;
	}

	trend(double slope, const item_t& i)
		: value(from_slope(slope))
		, slope(slope)
		, time(i.sample_time)
		, convert_waiting(i.convert_waiting)
		, convert_done_waiting(i.convert_done_waiting)
		, storage_ops_waiting(i.storage_ops_waiting)
		, retention_store_waiting(i.retention_store_waiting)
	{}

	trend()
		: value(unchanged)
		, slope(0)
		, time(0)
		, convert_waiting(0)
		, convert_done_waiting(0)
		, storage_ops_waiting(0)
		, retention_store_waiting(0)
	{}

	inline bool is_valid() const { return time != 0; }

	inline std::size_t get_backlog() const
	{
		return convert_waiting + convert_done_waiting + storage_ops_waiting + retention_store_waiting;
	}

	inline static trend make_from_samples(const boost::chrono::minutes& mins,
												std::size_t n_samples,
												const item_t* samples)
	{
		std::size_t n = std::min(std::min(n_samples, std::size_t(mins.count())), std::size_t(60));

		if (n < 3) {
			return trend();
		}

		runstats::regression regression;
		for (std::size_t i = 0; i < n; i++) {
			regression.push(i + 1, get_processing_count(samples[n - i - 1]));
		}

		return trend(regression.slope(), samples[0]);
	}
};

std::ostream&
operator<<(std::ostream& os, const trend& t)
{
	if (!t.is_valid()) {
		return os << "<unknown>";
	}

	return os << trend::value_str(t.value) << " [slope:" << t.slope << " convert-waiting:" << t.convert_waiting
			  << " convert-done-waiting:" << t.convert_done_waiting << " storage-ops-waiting:" << t.storage_ops_waiting
			  << " retention-store-waiting:" << t.retention_store_waiting << " total:" << t.get_backlog() << ']';
}

} // namespace stats
} // namespace archived
} // namespace common
#endif // ARCHIVING_COMMON_ARCHIVED_STATS_SNAPSHOT_HPP
