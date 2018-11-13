//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 09/10/2018.
//

#include <gtest/gtest.h>

#include "../../common/host_ip_change_detector.hpp"

TEST(TestBasic, BoolCast)
{
    archiving::ip_change_result _true(true, 1);
    ASSERT_TRUE(_true);

    archiving::ip_change_result _false(false, 1);
    ASSERT_FALSE(_false);

    if (_true) {
        ASSERT_TRUE(true);
    } else {
        ASSERT_TRUE(true);
    }
}
