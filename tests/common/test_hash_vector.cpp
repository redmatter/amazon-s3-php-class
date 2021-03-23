//
// Copyright (c) 2021 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 13/03/2021.
//

#include <gtest/gtest.h>

#include "../../common/types.hpp"

TEST(HashVector, Blank)
{
    archiving::hash_vector zero = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

    ASSERT_EQ(zero, archiving::hash_vector());
}
