//
// Copyright (c) 2021 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 11/03/2021.
//

#include "gtest/gtest.h"

#include "../archivable/utils.hpp"

TEST(TestDelayProducer, CallDuration2Delay)
{
    ASSERT_EQ(boost::chrono::seconds(10).count(), archiving::call_duration_to_delay(0U).count());
    ASSERT_EQ(boost::chrono::seconds(10).count(), archiving::call_duration_to_delay(10U).count());
    ASSERT_EQ(boost::chrono::seconds(10).count(), archiving::call_duration_to_delay(100U).count());
    ASSERT_EQ(boost::chrono::seconds(10).count(), archiving::call_duration_to_delay(120U).count());
    ASSERT_EQ(boost::chrono::seconds(11).count(), archiving::call_duration_to_delay(180U).count());
    ASSERT_EQ(boost::chrono::seconds(12).count(), archiving::call_duration_to_delay(boost::chrono::minutes(6)).count());
    ASSERT_EQ(boost::chrono::seconds(30).count(), archiving::call_duration_to_delay(boost::chrono::hours(1)).count());
}

TEST(TestDelayProducer, CalculateDelay)
{
    // ideal situation where the call finished and was enqueued straightaway..
    ASSERT_EQ(boost::chrono::seconds(10),
              archiving::call_duration_based_delayed_producer::calculate_delay(0, 0, boost::chrono::seconds(1)));
    ASSERT_EQ(boost::chrono::seconds(10),
              archiving::call_duration_based_delayed_producer::calculate_delay(0, 0, boost::chrono::seconds(10)));

    // A situation where the call finished and was enqueued after 11 seconds, which results in a negative delay.
    // In such circumstances the item is not delayed any further, since it has already been delayed while waiting
    // to be processed.
    ASSERT_EQ(boost::chrono::seconds(-1),
              archiving::call_duration_based_delayed_producer::calculate_delay(11, 0, boost::chrono::seconds(1)));
    ASSERT_EQ(boost::chrono::seconds(-1),
              archiving::call_duration_based_delayed_producer::calculate_delay(11, 0, boost::chrono::seconds(10)));

    // A situation where the call finished and was enqueued after 6 seconds, the delay will account for the time waiting
    // to be processed.
    ASSERT_EQ(boost::chrono::seconds(4),
              archiving::call_duration_based_delayed_producer::calculate_delay(11, 5, boost::chrono::seconds(1)));
    ASSERT_EQ(boost::chrono::seconds(4),
              archiving::call_duration_based_delayed_producer::calculate_delay(11, 5, boost::chrono::seconds(10)));
}
