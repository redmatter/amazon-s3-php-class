//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 18/02/2020.
//

#include "gtest/gtest.h"

#include "../../archived/net/response.hpp"
#include "../../common/net/archived/response/parsed.hpp"

const char* UUID1 = "00000000-0000-0000-0000-000000000001";
const char* AN_ERROR_MESSAGE = "silly bugs";

namespace put = archiving::archived::net::put;
namespace recv = archiving::archived::net::recv;
namespace response = common::net::archived::response;

using archiving::file_type;

TEST(PutResponse, ParseNoIdentifier)
{
	namespace ns = common::net::archived::put;

	std::stringstream ss;
	ss << put::internal_error(AN_ERROR_MESSAGE) << "\r\n";

	ns::response pr;
	ss >> pr;

	ASSERT_TRUE(ss.good());
	ASSERT_EQ(pr.message, AN_ERROR_MESSAGE);
	ASSERT_EQ(pr.code, response::internal_error);
}

TEST(PutResponse, ParseWithIdentifierNoMessage)
{
	namespace ns = common::net::archived::put;

	std::stringstream ss;
	ss << put::internal_error(file_type::cdr, 1, UUID1) << "\r\n";

	ns::response pr;
	ss >> pr;

	ASSERT_TRUE(ss.good());
	ASSERT_EQ(pr.file_type, file_type::cdr);
	ASSERT_EQ(pr.org_id, 1);
	ASSERT_EQ(pr.uuid, UUID1);
	ASSERT_EQ(pr.message, "internal server error");
	ASSERT_EQ(pr.code, response::internal_error);
}

TEST(PutResponse, ParseWithIdentifierWithMessage)
{
	namespace ns = common::net::archived::put;

	std::stringstream ss;
	ss << put::internal_error(file_type::cdr, 1, UUID1, AN_ERROR_MESSAGE) << "\r\n";

	ns::response pr;
	ss >> pr;

	ASSERT_TRUE(ss.good());
	ASSERT_EQ(pr.file_type, file_type::cdr);
	ASSERT_EQ(pr.org_id, 1);
	ASSERT_EQ(pr.uuid, UUID1);
	ASSERT_EQ(pr.message, AN_ERROR_MESSAGE);
	ASSERT_EQ(pr.code, response::internal_error);
}

TEST(PutResponse, ParseLegacyWithIdentifier)
{
	namespace ns = common::net::archived::put;

	std::stringstream ss;
	ss << "ERR PUT cdr 1 " << UUID1 << " " << AN_ERROR_MESSAGE << "\r\n";

	ns::response pr;
	ss >> pr;

	ASSERT_TRUE(ss.good());
	ASSERT_EQ(pr.file_type, file_type::cdr);
	ASSERT_EQ(pr.org_id, 1);
	ASSERT_EQ(pr.uuid, UUID1);
	ASSERT_EQ(pr.message, AN_ERROR_MESSAGE);
	ASSERT_EQ(pr.code, response::internal_error);
}

TEST(PutResponse, ParseLegacyNoIdentifier)
{
	namespace ns = common::net::archived::put;

	std::stringstream ss;
	ss << "ERR PUT " << AN_ERROR_MESSAGE << "\r\n";

	ns::response pr;
	ss >> pr;

	ASSERT_TRUE(ss.good());
	ASSERT_EQ(pr.file_type, file_type::unknown);
	ASSERT_EQ(pr.org_id, 0);
	ASSERT_EQ(pr.uuid, "");
	ASSERT_EQ(pr.message, AN_ERROR_MESSAGE);
	ASSERT_EQ(pr.code, response::internal_error);
}

TEST(PutResponse, ParseLegacyInvalidOrgID)
{
	namespace ns = common::net::archived::put;

	std::stringstream ss;
	ss << "ERR PUT cdr 0x01 " << UUID1 << " " << AN_ERROR_MESSAGE << "\r\n";

	ns::response pr;
	ss >> pr;

	ASSERT_FALSE(ss.good());
}

TEST(PutResponse, ParseLegacyInvalidLineEndingNoNL)
{
	namespace ns = common::net::archived::put;

	std::stringstream ss;
	ss << "ERR PUT cdr 1 " << UUID1 << " " << AN_ERROR_MESSAGE << "\r";

	ns::response pr;
	ss >> pr;

	ASSERT_FALSE(ss.good());
}

TEST(PutResponse, ParseLegacyInvalidLineEndingNoCR)
{
	namespace ns = common::net::archived::put;

	std::stringstream ss;
	ss << "ERR PUT cdr 1 " << UUID1 << " " << AN_ERROR_MESSAGE << "\n";

	ns::response pr;
	ss >> pr;

	ASSERT_FALSE(ss.good());
}

TEST(PutResponse, ParseContinue)
{
	namespace ns = common::net::archived::put;

	std::stringstream ss;
	ss << put::continue_(file_type::cdr, 1, UUID1) << "\r\n";

	ns::response pr;
	ss >> pr;

	ASSERT_TRUE(ss.good());
	ASSERT_EQ(pr.file_type, file_type::cdr);
	ASSERT_EQ(pr.org_id, 1);
	ASSERT_EQ(pr.uuid, UUID1);
	ASSERT_EQ(pr.message, "");
	ASSERT_EQ(pr.code, response::continue_);
}

TEST(RecvResponse, ParseAccepted)
{
	namespace ns = common::net::archived::recv;

	std::stringstream ss;
	ss << recv::accepted(file_type::cdr, 1, UUID1) << "\r\n";

	ns::response pr;
	ss >> pr;

	ASSERT_TRUE(ss.good());
	ASSERT_EQ(pr.file_type, file_type::cdr);
	ASSERT_EQ(pr.org_id, 1);
	ASSERT_EQ(pr.uuid, UUID1);
	ASSERT_EQ(pr.message, "");
	ASSERT_EQ(pr.code, response::accepted);
}
