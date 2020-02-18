//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 18/02/2020.
//

#include "gtest/gtest.h"

#include "../../archived/put/response.hpp"
#include "../../common/archived/put_response/parsed.hpp"

const char* UUID1 = "00000000-0000-0000-0000-000000000001";
const char* AN_ERROR_MESSAGE = "silly bugs";

namespace ns = common::archived::put_response;
namespace put = archiving::archived::put;
using archiving::file_type;

TEST(PutResponse, ParseNoIdentifier)
{
	std::stringstream ss;
	ss << put::internal_error(AN_ERROR_MESSAGE) << "\r\n";

	ns::parsed_response pr;
	ss >> pr;

	ASSERT_TRUE(ss.good());
	ASSERT_EQ(pr.message, AN_ERROR_MESSAGE);
	ASSERT_EQ(pr.code, ns::internal_error);
}

TEST(PutResponse, ParseWithIdentifierNoMessage)
{
	std::stringstream ss;
	ss << put::internal_error(file_type::cdr, 1, UUID1) << "\r\n";

	ns::parsed_response pr;
	ss >> pr;

	ASSERT_TRUE(ss.good());
	ASSERT_EQ(pr.file_type, file_type::cdr);
	ASSERT_EQ(pr.org_id, 1);
	ASSERT_EQ(pr.uuid, UUID1);
	ASSERT_EQ(pr.message, "internal server error");
	ASSERT_EQ(pr.code, ns::internal_error);
}

TEST(PutResponse, ParseWithIdentifierWithMessage)
{
	std::stringstream ss;
	ss << put::internal_error(file_type::cdr, 1, UUID1, AN_ERROR_MESSAGE) << "\r\n";

	ns::parsed_response pr;
	ss >> pr;

	ASSERT_TRUE(ss.good());
	ASSERT_EQ(pr.file_type, file_type::cdr);
	ASSERT_EQ(pr.org_id, 1);
	ASSERT_EQ(pr.uuid, UUID1);
	ASSERT_EQ(pr.message, AN_ERROR_MESSAGE);
	ASSERT_EQ(pr.code, ns::internal_error);
}

TEST(PutResponse, ParseLegacyWithIdentifier)
{
	std::stringstream ss;
	ss << "ERR PUT cdr 1 " << UUID1 << " " << AN_ERROR_MESSAGE << "\r\n";

	ns::parsed_response pr;
	ss >> pr;

	ASSERT_TRUE(ss.good());
	ASSERT_EQ(pr.file_type, file_type::cdr);
	ASSERT_EQ(pr.org_id, 1);
	ASSERT_EQ(pr.uuid, UUID1);
	ASSERT_EQ(pr.message, AN_ERROR_MESSAGE);
	ASSERT_EQ(pr.code, ns::internal_error);
}

TEST(PutResponse, ParseLegacyNoIdentifier)
{
	std::stringstream ss;
	ss << "ERR PUT " << AN_ERROR_MESSAGE << "\r\n";

	ns::parsed_response pr;
	ss >> pr;

	ASSERT_TRUE(ss.good());
	ASSERT_EQ(pr.file_type, file_type::unknown);
	ASSERT_EQ(pr.org_id, 0);
	ASSERT_EQ(pr.uuid, "");
	ASSERT_EQ(pr.message, AN_ERROR_MESSAGE);
	ASSERT_EQ(pr.code, ns::internal_error);
}

TEST(PutResponse, ParseLegacyInvalidOrgID)
{
	std::stringstream ss;
	ss << "ERR PUT cdr 0x01 " << UUID1 << " " << AN_ERROR_MESSAGE << "\r\n";

	ns::parsed_response pr;
	ss >> pr;

	ASSERT_FALSE(ss.good());
}

TEST(PutResponse, ParseLegacyInvalidLineEndingNoNL)
{
	std::stringstream ss;
	ss << "ERR PUT cdr 1 " << UUID1 << " " << AN_ERROR_MESSAGE << "\r";

	ns::parsed_response pr;
	ss >> pr;

	ASSERT_FALSE(ss.good());
}

TEST(PutResponse, ParseLegacyInvalidLineEndingNoCR)
{
	std::stringstream ss;
	ss << "ERR PUT cdr 1 " << UUID1 << " " << AN_ERROR_MESSAGE << "\n";

	ns::parsed_response pr;
	ss >> pr;

	ASSERT_FALSE(ss.good());
}

TEST(PutResponse, ParseContinue)
{
	std::stringstream ss;
	ss << put::continue_(file_type::cdr, 1, UUID1) << "\r\n";

	ns::parsed_response pr;
	ss >> pr;

	ASSERT_TRUE(ss.good());
	ASSERT_EQ(pr.file_type, file_type::cdr);
	ASSERT_EQ(pr.org_id, 1);
	ASSERT_EQ(pr.uuid, UUID1);
	ASSERT_EQ(pr.verb, "PUT");
	ASSERT_EQ(pr.message, "");
	ASSERT_EQ(pr.code, ns::continue_);
}

TEST(PutResponse, ParseAccepted)
{
	std::stringstream ss;
	ss << put::accepted(file_type::cdr, 1, UUID1) << "\r\n";

	ns::parsed_response pr;
	ss >> pr;

	ASSERT_TRUE(ss.good());
	ASSERT_EQ(pr.file_type, file_type::cdr);
	ASSERT_EQ(pr.org_id, 1);
	ASSERT_EQ(pr.uuid, UUID1);
	ASSERT_EQ(pr.verb, "RECV");
	ASSERT_EQ(pr.message, "");
	ASSERT_EQ(pr.code, ns::accepted);
}
