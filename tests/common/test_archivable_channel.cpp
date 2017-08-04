#include "../../common/archivable.hpp"

#include "gtest/gtest.h"

int version_number[4];

TEST(TestSetup, ProcudeAndConsume)
{
    archiving::archivable_channel_t c("test");
    c.produce(archiving::archivable::pointer(
            new archiving::archivable("00000000-0000-0000-0000-000000000000", archiving::file_type::recording)));
    sleep(1);
    c.produce(archiving::archivable::pointer(
            new archiving::archivable("00000000-0000-0000-0000-000000000001", archiving::file_type::cdr)));

    ASSERT_EQ(c.consume()->get_type(), archiving::file_type::cdr);
    ASSERT_EQ(c.consume()->get_type(), archiving::file_type::recording);
}

/*
 CDRs get higher priority
 Each type has a maximum delay
 Till the max delay, they are sorted by type, CDR getting the max priority
 For items whose max delay has hit, they are sent in a best effort manner

 Ideas:
  - Back the PC channel with SQLite  ... so that dynamic ordering will work (can use memory tables for performance)

 Look at multifactor job scheduling algorithms like one in slurm

 */