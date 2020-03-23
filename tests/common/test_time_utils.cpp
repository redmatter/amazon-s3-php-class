//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 19/02/2020.
//

#include "gtest/gtest.h"
#include "../../common/utils/time.hpp"
#include <boost/chrono/round.hpp>

TEST(TimeUtils, TimeConversion)
{
	using boost::chrono::duration_cast;
	using boost::chrono::seconds;
	using boost::chrono::steady_clock;
	using boost::chrono::system_clock;
	using common::utils::to_steady_clock;
	using common::utils::to_system_clock;
	using common::utils::to_time_t;

	const steady_clock::time_point steady_time = steady_clock::now() + seconds(10);
	const system_clock::time_point sys_time = to_system_clock(steady_time);

	ASSERT_EQ(duration_cast<seconds>(sys_time.time_since_epoch()),
			  system_clock::from_time_t(to_time_t(steady_time)).time_since_epoch());
	ASSERT_EQ(to_time_t(steady_time), to_time_t(sys_time));
}

TEST(TimeUtils, ToSteadyClock)
{
	using boost::chrono::milliseconds;
	using boost::chrono::seconds;
	using boost::chrono::steady_clock;
	using boost::chrono::system_clock;
	using common::utils::to_steady_clock;

	const system_clock::time_point t1 = system_clock::now();
	const system_clock::time_point t2 = t1 + seconds(10);
	const steady_clock::time_point steady_t1 = to_steady_clock(t1);
	const steady_clock::time_point steady_t2 = to_steady_clock(t2);

	ASSERT_EQ(boost::chrono::round<milliseconds>(t2 - t1), boost::chrono::round<milliseconds>(steady_t2 - steady_t1));
}

TEST(TimeUtils, SteadyClockToSteadyClock)
{
	using boost::chrono::seconds;
	using boost::chrono::steady_clock;
	using common::utils::to_steady_clock;

	const steady_clock::time_point t1 = steady_clock::now();
	const steady_clock::time_point t2 = t1 + seconds(10);
	const steady_clock::time_point steady_t1 = to_steady_clock(t1);
	const steady_clock::time_point steady_t2 = to_steady_clock(t2);

	ASSERT_EQ((t2 - t1), (steady_t2 - steady_t1));
}

TEST(TimeUtils, ToSystemClock)
{
	using boost::chrono::milliseconds;
	using boost::chrono::seconds;
	using boost::chrono::steady_clock;
	using boost::chrono::system_clock;
	using common::utils::to_system_clock;

	const steady_clock::time_point t1 = steady_clock::now();
	const steady_clock::time_point t2 = t1 + seconds(10);
	const system_clock::time_point system_t1 = to_system_clock(t1);
	const system_clock::time_point system_t2 = to_system_clock(t2);

	ASSERT_EQ(boost::chrono::round<milliseconds>(t2 - t1), boost::chrono::round<milliseconds>(system_t2 - system_t1));
}

TEST(TimeUtils, SystemClockToSystemClock)
{
	using boost::chrono::seconds;
	using boost::chrono::system_clock;
	using common::utils::to_system_clock;

	const system_clock::time_point t1 = system_clock::now();
	const system_clock::time_point t2 = t1 + seconds(10);
	const system_clock::time_point system_t1 = to_system_clock(t1);
	const system_clock::time_point system_t2 = to_system_clock(t2);

	ASSERT_EQ((t2 - t1), (system_t2 - system_t1));
}

TEST(TimeUtils, TimeTToSystemClock)
{
	using boost::chrono::seconds;
	using boost::chrono::system_clock;
	using common::utils::to_system_clock;

	std::time_t t1 = std::time(0);
	std::time_t t2 = t1 + 10;

	const system_clock::time_point system_t1 = to_system_clock(t1);
	const system_clock::time_point system_t2 = to_system_clock(t2);

	ASSERT_EQ(seconds(t2 - t1), (system_t2 - system_t1));
}

TEST(TimeUtils, TimeTToSteadyClock)
{
	using boost::chrono::seconds;
	using boost::chrono::steady_clock;
	using common::utils::to_steady_clock;

	std::time_t t1 = std::time(0);
	std::time_t t2 = t1 + 10;

	const steady_clock::time_point steady_t1 = to_steady_clock(t1);
	const steady_clock::time_point steady_t2 = to_steady_clock(t2);

	ASSERT_EQ(seconds(t2 - t1), boost::chrono::round<seconds>(steady_t2 - steady_t1));
}
