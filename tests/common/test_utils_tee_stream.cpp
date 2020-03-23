//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 23/02/2020.
//

#include "../../common/utils/tee_stream.hpp"
#include "gtest/gtest.h"

TEST(TeeIstream, Basics)
{
	namespace ns = common::utils;

	std::istringstream istr("2020 Year of the rat");
	std::ostringstream ostr;

	std::string str;
	ns::tee_istream ti(istr, ostr);
	ti >> str >> str >> str >> str >> str;
	(void)str;

	ASSERT_STREQ(ostr.str().c_str(), "2020 Year of the rat");

	ASSERT_TRUE(istr.good());
	ASSERT_TRUE(ostr.good());
}

TEST(TeeOstream, Basics)
{
	namespace ns = common::utils;
	std::ostringstream ostr1;
	std::ostringstream ostr2;

	ns::tee_ostream to(ostr1, ostr2);
	to << "Woo hoo!!";

	ASSERT_STREQ(ostr1.str().c_str(), "Woo hoo!!");
	ASSERT_STREQ(ostr2.str().c_str(), "Woo hoo!!");

	ASSERT_TRUE(ostr1.good());
	ASSERT_TRUE(ostr2.good());
}
