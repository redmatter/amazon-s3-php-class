#include "../../estuary/app/error.hpp"

#include "gtest/gtest.h"

int version_number[4];

TEST(TestHttpErrors, Basic)
{
    ASSERT_EQ(
                boost::system::error_condition(app::error::http::forbidden),
                boost::system::error_condition(app::error::http::forbidden, app::error::get_http_category()));
    ASSERT_EQ(
                boost::system::error_condition(app::error::http::forbidden).category(),
                app::error::get_http_category());
}
