//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 19/02/2020.
//

#include "gtest/gtest.h"
#include "../../common/utils/time.hpp"

TEST(TimeUtils, TimeConversion)
{
	using common::utils::to_system_clock;
	using common::utils::to_time_t;
	using boost::chrono::duration_cast;
	using boost::chrono::seconds;
	using boost::chrono::steady_clock;
	using boost::chrono::system_clock;

	const steady_clock::time_point steady_time = steady_clock::now() + seconds(10);
	const system_clock::time_point sys_time = to_system_clock(steady_time);

	ASSERT_EQ(duration_cast<seconds>(sys_time.time_since_epoch()),
			  system_clock::from_time_t(to_time_t(steady_time)).time_since_epoch());
	ASSERT_EQ(to_time_t(steady_time), to_time_t(sys_time));
}
