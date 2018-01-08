//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 21/12/2017.
//

#ifndef ARCHIVING_ESTUARY_APP_ARCHIVABLE_QUEUE_HPP
#define ARCHIVING_ESTUARY_APP_ARCHIVABLE_QUEUE_HPP

#include "error.hpp"
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

boost::shared_ptr<archiving::archivable> consume_archivable(
        const boost::shared_ptr<archivable_queue>& queue,
        const boost::chrono::milliseconds& wait_timeout,
        const bool _ignored_,
        const boost::function0<void>& timeout_callback,
        const boost::function0<void>& is_finishing_callback)
{
    boost::shared_ptr<archiving::archivable> item;
    boost::system::error_condition ec;
    queue->consume(item, wait_timeout, ec);
    if (ec) {
        if (ec.value() == queue::error::runtime_errors::consume_timeout) {
            timeout_callback();
        } else if (ec.value() == queue::error::runtime_errors::is_finishing) {
            is_finishing_callback();
        } else {
            throw boost::system::system_error(ec.value(), ec.category());
        }
    }

    return item;
}

void produce_response_on_error(const boost::shared_ptr<response_queue>& queue,
                               const boost::shared_ptr<archiving::archivable>& archivable, const bool _ignored_)
{
    boost::system::error_condition error;
    queue->produce(
            response(archivable->get_database_id(), boost::system::error_condition(error::http::internal_server_error)),
            error);
    if (error) {
        LogError("Error scheduling response; " << error);
    }
}

void produce_response_on_success(const boost::shared_ptr<response_queue>& queue,
                               const boost::shared_ptr<archiving::archivable>& archivable)
{
    boost::system::error_condition error;
    queue->produce(
            response(archivable->get_database_id(), boost::system::error_condition(error::http::no_content)), error);
    if (error) {
        LogError("Error scheduling response; " << error);
    }
}

void dummy_remove_from_catalogue(const boost::shared_ptr<archiving::archivable>& archivable)
{
    (void)archivable;
}

bool dummy_save_to_database(const boost::shared_ptr<archiving::archivable>& archivable)
{
    (void)archivable;
    return true;
}

} // namespace app

#endif //ARCHIVING_ESTUARY_APP_ARCHIVABLE_QUEUE_HPP
