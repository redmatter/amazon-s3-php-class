//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 16/02/2020.
//

#include "../archived/put/response.hpp"
#include "gtest/gtest.h"

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

TEST(PutResponse, TimeConversion)
{
	using archiving::archived::detail::to_system_clock;
	using archiving::archived::detail::to_time_t;
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

TEST(PutResponse, Basics)
{
	namespace ns = archiving::archived::put;
	using archiving::file_type;
	using archiving::archived::detail::to_system_clock;
	using archiving::archived::detail::to_time_t;
	using boost::format;
	using boost::lexical_cast;
	using boost::str;
	using boost::chrono::duration_cast;
	using boost::chrono::minutes;
	using boost::chrono::seconds;
	using boost::chrono::steady_clock;
	using boost::chrono::system_clock;

	const format err("ERR PUT cdr 1 00000000-0000-0000-0000-000000000001 [%1%] %2%");
	const format err_basic("ERR PUT [%1%] %2%");
	const format err_with_retry_after("ERR PUT cdr 1 00000000-0000-0000-0000-000000000001 [%1% retry-after:%2%] %3%");

	const steady_clock::time_point steady_retry_after = steady_clock::now() + minutes(10);
	const system_clock::time_point sys_retry_after = to_system_clock(steady_retry_after);
	const std::time_t time_t_retry_after = to_time_t(steady_retry_after);

	ASSERT_EQ(lexical_cast<std::string>(ns::continue_(file_type::cdr, 1, "00000000-0000-0000-0000-000000000001")),
			  "OK PUT cdr 1 00000000-0000-0000-0000-000000000001");

	ASSERT_EQ(lexical_cast<std::string>(ns::accepted(file_type::cdr, 1, "00000000-0000-0000-0000-000000000001")),
			  "OK RECV cdr 1 00000000-0000-0000-0000-000000000001");

	ASSERT_EQ(lexical_cast<std::string>(ns::bad_request("unknown error")),
			  str(format(err_basic) % ns::response::bad_request % "unknown error"));
	ASSERT_EQ(lexical_cast<std::string>(
				  ns::bad_request(file_type::cdr, 1, "00000000-0000-0000-0000-000000000001", "unknown error")),
			  str(format(err) % ns::response::bad_request % "unknown error"));

	ASSERT_EQ(lexical_cast<std::string>(ns::internal_error(file_type::cdr, 1, "00000000-0000-0000-0000-000000000001")),
			  str(format(err) % ns::response::internal_error % "internal server error"));
	ASSERT_EQ(lexical_cast<std::string>(ns::internal_error(file_type::cdr, 1, "00000000-0000-0000-0000-000000000001", "unknown error")),
			  str(format(err) % ns::response::internal_error % "unknown error"));
	ASSERT_EQ(lexical_cast<std::string>(ns::internal_error("unknown error")),
			  str(format(err_basic) % ns::response::internal_error % "unknown error"));

	ASSERT_EQ(lexical_cast<std::string>(
				  ns::server_busy(file_type::cdr, 1, "00000000-0000-0000-0000-000000000001", sys_retry_after)),
			  str(format(err_with_retry_after) % ns::response::server_busy % time_t_retry_after % "server busy"));

	ASSERT_EQ(lexical_cast<std::string>(
				  ns::server_busy(file_type::cdr, 1, "00000000-0000-0000-0000-000000000001", steady_retry_after)),
			  str(format(err_with_retry_after) % ns::response::server_busy % time_t_retry_after % "server busy"));

	ASSERT_EQ(
		lexical_cast<std::string>(ns::insufficient_storage(file_type::cdr, 1, "00000000-0000-0000-0000-000000000001")),
		str(format(err) % ns::response::insufficient_storage % "not enough space on disk"));
}
