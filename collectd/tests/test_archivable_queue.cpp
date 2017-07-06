//
// Created by dkorah on 03/07/17.
//

#include "gtest/gtest.h"

#include "../archivable/queue.hpp"

#include <boost/chrono/chrono_io.hpp>

collectd::archivable::queue::classifier_type::classification_type classify(const archiving::archivable::pointer &p)
{
    switch (p->get_type()) {
        case archiving::file_type::cdr:
            return 0;
        case archiving::file_type::buffered_recording:
            return 1;
        case archiving::file_type::recording:
            return 2;
        default:
            return 3;
    }
}

boost::chrono::nanoseconds get_deadline(const archiving::archivable::pointer &p)
{
    using namespace boost::chrono;
    return duration_cast<nanoseconds>(seconds((p->get_type() == archiving::file_type::cdr) ? 1 : 10));
}

TEST(TestArchivableQueue, Basic)
{
    boost::shared_ptr<collectd::archivable::catalogue> catalogue = boost::make_shared<collectd::archivable::catalogue>();
    boost::shared_ptr<collectd::archivable::queue> q = boost::make_shared<collectd::archivable::queue>(
            "test",
            catalogue,
            boost::bind(get_deadline, _1)
    );

    boost::system::error_condition ec;
    const boost::chrono::milliseconds _10ms(10);

    q->produce(
            boost::make_shared<archiving::archivable>(
                    "9ca6db95-bfdc-4dc1-865f-91e6c9479bc0", archiving::file_type::recording),
            _10ms, true, ec);
    ASSERT_FALSE(ec);

    // make sure a duplicate item is not added to the queue and an error is flagged
    q->produce(
            boost::make_shared<archiving::archivable>(
                    "9ca6db95-bfdc-4dc1-865f-91e6c9479bc0", archiving::file_type::recording),
            _10ms, true, ec);
    ASSERT_TRUE(ec);
    ASSERT_EQ(q->count(), 1);

    // make sure items with same UUID but different file-type are not counted as duplicates
    q->produce(
            boost::make_shared<archiving::archivable>(
                    "00000001-bfdc-4dc1-865f-91e6c9479bc0", archiving::file_type::cdr),
            _10ms, true, ec);
    q->produce(
            boost::make_shared<archiving::archivable>(
                    "00000001-bfdc-4dc1-865f-91e6c9479bc0", archiving::file_type::recording),
            _10ms, true, ec);
    ASSERT_EQ(q->count(), 3);

    archiving::archivable::pointer item;
    q->consume(item, _10ms, true, ec);
    ASSERT_FALSE(ec);

    // make sure CDR gets the highest priority
    ASSERT_EQ(item->get_type(), archiving::file_type::cdr);

    ASSERT_EQ(catalogue->count(), 2);
    ASSERT_EQ(catalogue->count(), q->count());
}
