//
// Copyright (c) 2021 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 11/03/2021.
//

#include "gtest/gtest.h"

#include "../archivable/utils.hpp"

TEST(TestArchivableDelay, Basics)
{
    ASSERT_EQ(boost::chrono::seconds(10).count(), archiving::call_duration_to_delay(0U).count());
    ASSERT_EQ(boost::chrono::seconds(10).count(), archiving::call_duration_to_delay(10U).count());
    ASSERT_EQ(boost::chrono::seconds(10).count(), archiving::call_duration_to_delay(100U).count());
    ASSERT_EQ(boost::chrono::seconds(10).count(), archiving::call_duration_to_delay(120U).count());
    ASSERT_EQ(boost::chrono::seconds(11).count(), archiving::call_duration_to_delay(180U).count());
    ASSERT_EQ(boost::chrono::seconds(12).count(), archiving::call_duration_to_delay(boost::chrono::minutes(6)).count());
    ASSERT_EQ(boost::chrono::seconds(30).count(), archiving::call_duration_to_delay(boost::chrono::hours(1)).count());
}
