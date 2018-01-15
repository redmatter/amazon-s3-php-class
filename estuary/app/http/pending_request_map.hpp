//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 11/01/2018.
//

#ifndef ARCHIVING_PENDING_REQUEST_MAP_HPP
#define ARCHIVING_PENDING_REQUEST_MAP_HPP

#include "pending_request.hpp"

#include <boost/unordered/unordered_map.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/lock_types.hpp>

#include <stdint.h>

namespace app
{
namespace http
{

#pragma clang diagnostic push
#pragma ide diagnostic ignored "TemplateArgumentsIssues"
struct pending_request_map : public boost::unordered_map<uint64_t, boost::shared_ptr<const pending_request> >
{
    void add(const boost::shared_ptr<const pending_request>& p)
    {
        boost::unique_lock<boost::mutex> lock(mutex);
        insert(std::make_pair(p->correlation.id, p));
    }

    boost::shared_ptr<const pending_request> lookup(const uint64_t id) const
    {
        boost::unique_lock<boost::mutex> lock(mutex);
        const_iterator itr = find(id);
        if (itr == end()) {
            return boost::shared_ptr<const pending_request>();
        }

        return itr->second;
    }

    void remove(const uint64_t id)
    {
        boost::unique_lock<boost::mutex> lock(mutex);
        const_iterator itr = find(id);
        if (itr != end()) {
            erase(itr);
        }
    }

    void remove(const boost::shared_ptr<const pending_request>& p)
    {
        remove(p->correlation.id);
    }

private:
    mutable boost::mutex mutex;
};
#pragma clang diagnostic pop

} // namespace http
} // namespace app
#endif //ARCHIVING_PENDING_REQUEST_MAP_HPP
