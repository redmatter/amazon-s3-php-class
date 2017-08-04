//
// Created by dkorah on 04/07/17.
//

#include "gtest/gtest.h"

#include "../archivable/catalogue.hpp"

TEST(TestCatalogue, Basic)
{
    const std::string uuid("8711c336-70d8-46f4-85f7-3b012a836751");
    boost::shared_ptr<collectd::archivable::catalogue> x = boost::make_shared<collectd::archivable::catalogue>();
    {
        // add a recording
        collectd::archivable::catalogue::item_sentry a(x, boost::make_shared<archiving::archivable>(
                uuid, archiving::file_type::recording));
        // adding an item with same UUID but different file-type will not throw
        ASSERT_NO_THROW(
                collectd::archivable::catalogue::item_sentry
                b(x, boost::make_shared<archiving::archivable>(uuid, archiving::file_type::cdr))
        );
        // adding an item with same UUID and file-type will throw
        bool thrown = false;
        try {
            archiving::archivable::pointer cdr =
                    boost::make_shared<archiving::archivable>(uuid, archiving::file_type::cdr);
            collectd::archivable::catalogue::item_sentry b(x, cdr), c(x, cdr);
        } catch (const boost::system::system_error &e) {
            thrown = true;
        }
        ASSERT_EQ(thrown, true);

        // it is possible to suppress the exception when the item is duplicate
        ASSERT_NO_THROW(
                collectd::archivable::catalogue::item_sentry
                d(x, boost::make_shared<archiving::archivable>(
                        uuid, archiving::file_type::recording), false)
        );

        // lack of exception does not mean that it was added
        ASSERT_EQ(x->count(), 1);
    }

    // assert that it is cleared down
    ASSERT_EQ(x->count(), 0);

    // assert that the sentry can be cancelled
    {
        collectd::archivable::catalogue::item_sentry e(x, boost::make_shared<archiving::archivable>(
                uuid, archiving::file_type::recording), false);
        e.cancel();
    }
    ASSERT_EQ(x->count(), 1);
}
