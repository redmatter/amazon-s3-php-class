//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 09 February 2020.
//

#ifndef ARCHIVING_COMMON_ARCHIVED_STATS_ITEM_HPP
#define ARCHIVING_COMMON_ARCHIVED_STATS_ITEM_HPP
#include <cstddef>
#include <ctime>
#include <iostream>
namespace common {
namespace archived {
namespace stats {

struct counter_t
{
    std::size_t put;
    std::size_t cached_only;
    std::size_t stored;
    std::size_t stored_size;
    std::size_t call_buffering;

    counter_t()
      : put(0)
      , cached_only(0)
      , stored(0)
      , stored_size(0)
      , call_buffering(0)
    {}

    inline void increment_put(std::size_t step = 1) { put += step; }

    inline void increment_cached_only(std::size_t step = 1) { cached_only += step; }

    inline void increment_stored(std::size_t step = 1) { stored += step; }

    inline void increment_stored_size(std::size_t step = 1) { stored_size += step; }

    inline void increment_call_buffering(std::size_t step = 1) { call_buffering += step; }
};

struct item_t
{
    std::time_t sample_time;

    std::size_t convert_waiting;
    std::size_t convert_done_waiting;
    std::size_t storage_ops_waiting;
    std::size_t retention_store_waiting;
    std::size_t purge_waiting;
    std::size_t whisper_waiting;

    bool is_db_loading;

    std::size_t store_put_count;
    std::size_t store_put_size;
    std::size_t store_convert_count;
    std::size_t store_save_count;
    std::size_t store_retention_count;

    counter_t counter;

    item_t()
      : sample_time(0)

      , convert_waiting(0)
      , convert_done_waiting(0)
      , storage_ops_waiting(0)
      , retention_store_waiting(0)
      , purge_waiting(0)
      , whisper_waiting(0)

      , is_db_loading(false)

      , store_put_count(0)
      , store_put_size(0)
      , store_convert_count(0)
      , store_save_count(0)
      , store_retention_count(0)

      , counter()
    {}
};

inline std::ostream&
operator<<(std::ostream& ostr, const item_t& item)
{
    return ostr << item.sample_time << " Convert:" << item.convert_waiting
                << " ConvertDone:" << item.convert_done_waiting << " StorageOps:" << item.storage_ops_waiting
                << " RetentionData:" << item.retention_store_waiting << " Purge:" << item.purge_waiting
                << " Whisper:" << item.whisper_waiting << " DBLoading:" << (item.is_db_loading ? "yes" : "no")
                << " counters:" << item.counter.put << '/' << item.counter.cached_only << '/' << item.counter.stored
                << ' ' << item.counter.stored_size << " CallBuffering:" << item.counter.call_buffering;
}

} // namespace stats
} // namespace archived
} // namespace common
#endif // ARCHIVING_COMMON_ARCHIVED_STATS_ITEM_HPP
