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
#include "http/request_progress.hpp"

#include <boost/optional.hpp>

namespace app
{

struct response
{
    const uint64_t request_id;
    const boost::system::error_condition upshot;
    const std::string message;

    response(const uint64_t request_id, const boost::system::error_condition& upshot, const std::string& message) :
            request_id(request_id), upshot(upshot), message(message)
    {}

    response(const uint64_t request_id, const boost::system::error_condition& upshot) :
            request_id(request_id), upshot(upshot)
    {}

private:
    response();

    response(const response& other);

    response& operator=(const response& other);
};

std::ostream& operator<<(std::ostream& os, const response& r)
{
    os << '"' << r.upshot.message() << '"';
    if (!r.message.empty()) {
        os << " Message: \"" << r.message << '"';
    }

    return os;
}

typedef queue::base_queue<boost::shared_ptr<archiving::archivable> > archivable_queue;
typedef queue::base_queue<boost::shared_ptr<response> > response_queue;

boost::shared_ptr<archiving::archivable> consume_archivable(
        const boost::shared_ptr<archivable_queue>& queue,
        const boost::chrono::milliseconds& wait_timeout,
        const bool _ignored_,
        const boost::function0<void>& timeout_callback,
        const boost::function0<void>& is_finishing_callback,
        const boost::function<void(const uint64_t, app::http::request_progress::enum_t)>& mark_progress)
{
    boost::shared_ptr<archiving::archivable> item;
    boost::system::error_condition ec;
    queue->consume(item, wait_timeout, ec);
    if (ec) {
        if (ec.value() == queue::error::runtime_errors::is_empty) {
            timeout_callback();
        } else if (ec.value() == queue::error::runtime_errors::is_finishing) {
            is_finishing_callback();
        } else {
            throw boost::system::system_error(ec.value(), ec.category());
        }
    }

    if (item) {
        mark_progress(item->get_database_id(), app::http::request_progress::put_dequeued);
    }

    LogDebug(item << " sending to ArchiveD (if not NULL)");

    return item;
}

void produce_response_on_error(const boost::shared_ptr<response_queue>& queue,
                               const boost::shared_ptr<archiving::archivable>& archivable, const bool _ignored_)
{
    boost::system::error_condition error;

    LogDebug(archivable << " .. there was an error; setting up response");

    queue->produce(boost::make_shared<response>(archivable->get_database_id(),
                                                boost::system::error_condition(error::http::internal_server_error)),
                   error);
    if (error) {
        LogError("Error scheduling response; " << error);
    }
}

void produce_response_on_success(const boost::shared_ptr<response_queue>& queue,
                                 const boost::shared_ptr<archiving::archivable>& archivable,
                                 const boost::function<void(const uint64_t, app::http::request_progress::enum_t)>& mark_progress)
{
    boost::system::error_condition error;

    uint64_t id = archivable->get_database_id();

    mark_progress(id, app::http::request_progress::put_archived);

    LogDebug(archivable << " .. done; setting up response");

    queue->produce(boost::make_shared<response>(id, boost::system::error_condition(error::http::no_content)), error);
    if (error) {
        LogError("Error scheduling response; " << error);
    }
}

void dummy_remove_from_catalogue(const boost::shared_ptr<archiving::archivable>& archivable)
{
    (void) archivable;
}

bool dummy_save_to_database(const boost::shared_ptr<archiving::archivable>& archivable)
{
    (void) archivable;
    return true;
}

} // namespace app

#endif //ARCHIVING_ESTUARY_APP_ARCHIVABLE_QUEUE_HPP
