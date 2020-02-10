//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 10/02/2020.
//

#include "gtest/gtest.h"

#include "../../common/archived/stats/snapshot.hpp"

TEST(Trend, LessThanIdealNumberOfSamples)
{
	namespace ns = common::archived::stats;

	// newest sample at idx 0, and oldest at N
	ns::item_t samples[5];

	samples[0].sample_time = 5;
	samples[0].convert_waiting = 1;
	samples[0].convert_done_waiting = 1;
	samples[0].storage_ops_waiting = 1;
	samples[0].retention_store_waiting = 1;
	samples[0].purge_waiting = 1;
	samples[0].whisper_waiting = 1;

	samples[1].sample_time = 4;
	samples[1].convert_waiting = 2;
	samples[1].convert_done_waiting = 2;
	samples[1].storage_ops_waiting = 2;
	samples[1].retention_store_waiting = 2;
	samples[1].purge_waiting = 2;
	samples[1].whisper_waiting = 2;

	samples[2].sample_time = 3;
	samples[2].convert_waiting = 3;
	samples[2].convert_done_waiting = 3;
	samples[2].storage_ops_waiting = 3;
	samples[2].retention_store_waiting = 3;
	samples[2].purge_waiting = 3;
	samples[2].whisper_waiting = 3;

	samples[3].sample_time = 2;
	samples[3].convert_waiting = 4;
	samples[3].convert_done_waiting = 4;
	samples[3].storage_ops_waiting = 4;
	samples[3].retention_store_waiting = 4;
	samples[3].purge_waiting = 4;
	samples[3].whisper_waiting = 4;

	samples[4].sample_time = 1;
	samples[4].convert_waiting = 5;
	samples[4].convert_done_waiting = 5;
	samples[4].storage_ops_waiting = 5;
	samples[4].retention_store_waiting = 5;
	samples[4].purge_waiting = 5;
	samples[4].whisper_waiting = 5;

	ns::snapshot s(boost::chrono::minutes(6), sizeof(samples) / sizeof(ns::item_t), samples);
	ASSERT_LE(s.n_items, 6 /* mins */);
	ASSERT_EQ(s.n_items, 5);

	const ns::trend& t = s.get_trend();
	ASSERT_EQ(t.value, ns::trend::down);
}

TEST(Trend, MoreThanIdealNumberOfSamples)
{
	namespace ns = common::archived::stats;

	// newest sample at idx 0, and oldest at N
	ns::item_t samples[7];

	samples[0].sample_time = 7;
	samples[0].convert_waiting = 1;
	samples[0].convert_done_waiting = 1;
	samples[0].storage_ops_waiting = 1;
	samples[0].retention_store_waiting = 1;
	samples[0].purge_waiting = 1;
	samples[0].whisper_waiting = 1;

	samples[1].sample_time = 6;
	samples[1].convert_waiting = 2;
	samples[1].convert_done_waiting = 2;
	samples[1].storage_ops_waiting = 2;
	samples[1].retention_store_waiting = 2;
	samples[1].purge_waiting = 2;
	samples[1].whisper_waiting = 2;

	samples[2].sample_time = 5;
	samples[2].convert_waiting = 3;
	samples[2].convert_done_waiting = 3;
	samples[2].storage_ops_waiting = 3;
	samples[2].retention_store_waiting = 3;
	samples[2].purge_waiting = 3;
	samples[2].whisper_waiting = 3;

	samples[3].sample_time = 4;
	samples[3].convert_waiting = 4;
	samples[3].convert_done_waiting = 4;
	samples[3].storage_ops_waiting = 4;
	samples[3].retention_store_waiting = 4;
	samples[3].purge_waiting = 4;
	samples[3].whisper_waiting = 4;

	samples[4].sample_time = 3;
	samples[4].convert_waiting = 5;
	samples[4].convert_done_waiting = 5;
	samples[4].storage_ops_waiting = 5;
	samples[4].retention_store_waiting = 5;
	samples[4].purge_waiting = 5;
	samples[4].whisper_waiting = 5;

	samples[5].sample_time = 2;
	samples[5].convert_waiting = 6;
	samples[5].convert_done_waiting = 6;
	samples[5].storage_ops_waiting = 6;
	samples[5].retention_store_waiting = 6;
	samples[5].purge_waiting = 6;
	samples[5].whisper_waiting = 6;

	samples[6].sample_time = 1;
	samples[6].convert_waiting = 7;
	samples[6].convert_done_waiting = 7;
	samples[6].storage_ops_waiting = 7;
	samples[6].retention_store_waiting = 7;
	samples[6].purge_waiting = 7;
	samples[6].whisper_waiting = 7;

	ns::snapshot s(boost::chrono::minutes(6), sizeof(samples) / sizeof(ns::item_t), samples);
	ASSERT_LE(s.n_items, 6 /* mins */);
	ASSERT_EQ(s.n_items, 6);

	const ns::trend& t = s.get_trend();
	ASSERT_EQ(t.value, ns::trend::down);
}

TEST(Trend, LessThanNecessaryNumberOfSamples)
{
	namespace ns = common::archived::stats;

	// newest sample at idx 0, and oldest at N
	ns::item_t samples[2];

	samples[0].sample_time = 2;
	samples[0].convert_waiting = 1;
	samples[0].convert_done_waiting = 1;
	samples[0].storage_ops_waiting = 1;
	samples[0].retention_store_waiting = 1;
	samples[0].purge_waiting = 1;
	samples[0].whisper_waiting = 1;

	samples[1].sample_time = 1;
	samples[1].convert_waiting = 2;
	samples[1].convert_done_waiting = 2;
	samples[1].storage_ops_waiting = 2;
	samples[1].retention_store_waiting = 2;
	samples[1].purge_waiting = 2;
	samples[1].whisper_waiting = 2;

	ns::snapshot s(boost::chrono::minutes(6), sizeof(samples) / sizeof(ns::item_t), samples);
	ASSERT_EQ(s.n_items, 2);

	const ns::trend& t = s.get_trend();
	ASSERT_EQ(t.value, ns::trend::unchanged);
	ASSERT_FALSE(t.is_valid());
}
