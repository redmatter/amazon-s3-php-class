//
// Created by dkorah on 04/07/17.
//

#include "gtest/gtest.h"

#include "../archivable/catalogue.hpp"

TEST(TestCatalogue, Basic)
{
    collectd::archivable::catalogue x;
    {
        collectd::archivable::catalogue::item_sentry a(x, boost::make_shared<archiving::archivable>(
                "8711c336-70d8-46f4-85f7-3b012a836751", archiving::file_type::recording));
        ASSERT_NO_THROW(
                collectd::archivable::catalogue::item_sentry
                b(x, boost::make_shared<archiving::archivable>(
                        "8711c336-70d8-46f4-85f7-3b012a836751", archiving::file_type::cdr))
        );
        bool thrown = false;
        try {
            archiving::archivable::pointer cdr = boost::make_shared<archiving::archivable>(
                    "8711c336-70d8-46f4-85f7-3b012a836751", archiving::file_type::cdr);
            collectd::archivable::catalogue::item_sentry b(x, cdr), c(x, cdr);
        } catch (const boost::system::system_error &e) {
            thrown = true;
        }
        ASSERT_EQ(thrown, true);

        ASSERT_NO_THROW(
                collectd::archivable::catalogue::item_sentry
                d(x, boost::make_shared<archiving::archivable>(
                        "8711c336-70d8-46f4-85f7-3b012a836751", archiving::file_type::recording), false)
        );
        ASSERT_EQ(x.count(), 1);
    }
    ASSERT_EQ(x.count(), 0);
}
