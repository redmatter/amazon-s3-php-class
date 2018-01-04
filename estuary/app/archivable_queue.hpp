//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 21/12/2017.
//

#ifndef ARCHIVING_ESTUARY_APP_ARCHIVABLE_QUEUE_HPP
#define ARCHIVING_ESTUARY_APP_ARCHIVABLE_QUEUE_HPP

#include "queue/queue.hpp"
#include "../../common/archivable.hpp"

#include <boost/optional.hpp>

namespace app
{

struct response
{
    uint64_t id;
    boost::system::error_condition upshot;
    std::string message;

    response(const uint64_t& id, const boost::system::error_condition& upshot, const std::string& message) :
            id(id), upshot(upshot), message(message)
    {}

    response(const uint64_t& id, const boost::system::error_condition& upshot) : id(id), upshot(upshot)
    {}

    response(const response& other) : id(other.id), upshot(other.upshot), message(other.message)
    {}

    response& operator=(const response& other)
    {
        id = other.id;
        upshot = other.upshot;
        message = other.message;

        return *this;
    }

    response() : id(0)
    {}
};

typedef queue::base_queue<boost::shared_ptr<archiving::archivable> > archivable_queue;
typedef queue::base_queue<response> response_queue;
} // namespace app

#endif //ARCHIVING_ESTUARY_APP_ARCHIVABLE_QUEUE_HPP
