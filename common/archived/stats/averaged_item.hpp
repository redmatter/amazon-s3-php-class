//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 09 February 2020.
//

#ifndef ARCHIVING_COMMON_ARCHIVED_STATS_AVERAGED_ITEM_HPP
#define ARCHIVING_COMMON_ARCHIVED_STATS_AVERAGED_ITEM_HPP

#include "item.hpp"

#include <iostream>

namespace common {
namespace archived {
namespace stats {
struct averaged_item
{
    const float n_samples;
    float sample_count;

    time_t sample_time;

    float convert_waiting;
    float convert_done_waiting;
    float storage_ops_waiting;
    float retention_store_waiting;
    float purge_waiting;
    float whisper_waiting;

    bool is_db_loading;

    float store_put_count;
    float store_put_size;
    float store_convert_count;
    float store_save_count;
    float store_retention_count;

    float counter_put;
    float counter_cached_only;
    float counter_stored;
    float counter_stored_size;
    float counter_call_buffering;

    explicit averaged_item(const float n_samples)
      : n_samples(n_samples)
      , sample_count(0)
      , sample_time(0)
      , convert_waiting(0.0)
      , convert_done_waiting(0.0)
      , storage_ops_waiting(0.0)
      , retention_store_waiting(0.0)
      , purge_waiting(0.0)
      , whisper_waiting(0.0)
      , is_db_loading(false)
      , store_put_count(0.0)
      , store_put_size(0.0)
      , store_convert_count(0.0)
      , store_save_count(0.0)
      , store_retention_count(0.0)
      , counter_put(0.0)
      , counter_cached_only(0.0)
      , counter_stored(0.0)
      , counter_stored_size(0.0)
      , counter_call_buffering(0.0)
    {}

    void add_sample(const item_t& i)
    {
        if (sample_count >= n_samples) {
            return;
        }

        if (!sample_time) {
            sample_time = i.sample_time;
            is_db_loading = i.is_db_loading;
        }

        convert_waiting += (i.convert_waiting / n_samples);

        convert_done_waiting += (i.convert_done_waiting / n_samples);
        storage_ops_waiting += (i.storage_ops_waiting / n_samples);
        retention_store_waiting += (i.retention_store_waiting / n_samples);
        purge_waiting += (i.purge_waiting / n_samples);
        whisper_waiting += (i.whisper_waiting / n_samples);

        store_put_count += (i.store_put_count / n_samples);
        store_put_size += (i.store_put_size / n_samples);
        store_convert_count += (i.store_convert_count / n_samples);
        store_save_count += (i.store_save_count / n_samples);
        store_retention_count += (i.store_retention_count / n_samples);

        counter_put += (i.counter.put / n_samples);
        counter_cached_only += (i.counter.cached_only / n_samples);
        counter_stored += (i.counter.stored / n_samples);
        counter_stored_size += (i.counter.stored_size / n_samples);
        counter_call_buffering += (i.counter.call_buffering / n_samples);

        sample_count++;
    }
};

inline std::ostream&
operator<<(std::ostream& os, const averaged_item& item)
{
    if (item.sample_count != item.n_samples) {
        return os;
    }

    return os << int(item.n_samples) << " mins: " << item.sample_time << " Convert:" << item.convert_waiting
              << " ConvertDone:" << item.convert_done_waiting << " StorageOps:" << item.storage_ops_waiting
              << " RetentionData:" << item.retention_store_waiting << " Purge:" << item.purge_waiting
              << " Whisper:" << item.whisper_waiting << " DBLoading:" << (item.is_db_loading ? "yes" : "no")
              << " counters:" << item.counter_put << '/' << item.counter_cached_only << '/' << item.counter_stored
              << ' ' << item.counter_stored_size << " CallBuffering:" << item.counter_call_buffering << std::endl;
}

} // namespace stats
} // namespace archived
} // namespace common
#endif // ARCHIVING_COMMON_ARCHIVED_STATS_AVERAGED_ITEM_HPP
