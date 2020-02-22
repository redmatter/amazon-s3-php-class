//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 20/02/2020.
//

#include "gtest/gtest.h"
#include "../net/detail/retry.hpp"

#include <boost/chrono/system_clocks.hpp>

void print_int(int i)
{
    std::cerr << "got: " << i << std::endl;
}

TEST(RetryManager, Schedule)
{
    namespace ns = collectd::net::detail;
    using boost::chrono::seconds;
    using boost::chrono::steady_clock;

    ns::base_retry_manager<int> rm;

    steady_clock::time_point t = steady_clock::now();

    rm.add(t - seconds(10), 10);
    rm.add(t - seconds(9), 9);
    rm.add(t - seconds(8), 8);
    rm.add(t - seconds(7), 7);
    rm.add(t - seconds(6), 6);
    rm.add(t - seconds(5), 5);
    rm.add(t - seconds(4), 4);
    rm.add(t - seconds(3), 3);
    rm.add(t - seconds(2), 2);
    rm.add(t - seconds(1), 1);

    std::size_t c = rm.schedule_batch(t - seconds(5), 10, print_int);
    ASSERT_EQ(c, 6);
    ASSERT_EQ(rm.count(), 4);

    c = rm.schedule_batch(t - seconds(5), 10, print_int);
    ASSERT_EQ(c, 0);
    ASSERT_EQ(rm.count(), 4);

    rm.add(t - seconds(10), 10);
    ASSERT_EQ(rm.count(), 5);

    c = rm.schedule_batch(t - seconds(5), 10, print_int);
    ASSERT_EQ(c, 1);
    ASSERT_EQ(rm.count(), 4);
}
