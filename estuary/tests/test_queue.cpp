//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 22/08/2018.
//

#include <gtest/gtest.h>

#include "../app/queue/queue.hpp"
#include <boost/chrono/io/duration_io.hpp>
#include <boost/atomic.hpp>

typedef app::queue::base_queue<int> queue_type;

void produce(const boost::shared_ptr<queue_type>& q, std::ostringstream& os, boost::atomic_int& c)
{
    while (!boost::this_thread::interruption_requested()) {
        boost::system::error_condition ec;
        int seq = c.fetch_add(1);
        boost::chrono::steady_clock::time_point start(boost::chrono::steady_clock::now());
        q->produce(seq, boost::chrono::seconds(1), ec);
        boost::chrono::duration<float> took_seconds(boost::chrono::steady_clock::now() - start);
        if (ec) {
            os << "ERROR " << ec.message();
        } else {
            os << "produced: " << seq;
        }
        os << " in " << std::setprecision(5) << std::fixed << took_seconds << '\n';

        boost::this_thread::sleep_for(boost::chrono::seconds(1));
    }
}

void consume(const boost::shared_ptr<queue_type>& q, std::ostringstream& os, boost::atomic_int& c)
{
    while (!boost::this_thread::interruption_requested()) {
        boost::system::error_condition ec;
        boost::chrono::steady_clock::time_point start(boost::chrono::steady_clock::now());
        int i;
        q->consume(i, boost::chrono::seconds(1), ec);
        boost::chrono::duration<float> took_seconds(boost::chrono::steady_clock::now() - start);
        if (ec) {
            os << "ERROR " << ec.message();
        } else {
            os << "consumed: " << i;
        }
        os << " in " << std::setprecision(5) << std::fixed << took_seconds << '\n';

        boost::this_thread::sleep_for(boost::chrono::seconds(3));
    }
}

TEST(Queue, Threaded)
{
    boost::thread_group tg;
    boost::shared_ptr<queue_type> q = boost::make_shared<queue_type>(2);

    boost::atomic_int c(0);

    std::ostringstream pos, cos;

    tg.create_thread(boost::bind(produce, q, boost::ref(pos), boost::ref(c)));
    tg.create_thread(boost::bind(produce, q, boost::ref(pos), boost::ref(c)));

    tg.create_thread(boost::bind(consume, q, boost::ref(cos), boost::ref(c)));
    tg.create_thread(boost::bind(consume, q, boost::ref(cos), boost::ref(c)));
    tg.create_thread(boost::bind(consume, q, boost::ref(cos), boost::ref(c)));

    boost::this_thread::sleep_for(boost::chrono::seconds(20));
    tg.interrupt_all();
    tg.join_all();

    std::cout << pos.str() << std::endl;
    std::cout << cos.str() << std::endl;
}
