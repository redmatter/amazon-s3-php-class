//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 22/08/2018.
//

#ifndef ARCHIVING_REQUEST_PROGRESS_HPP
#define ARCHIVING_REQUEST_PROGRESS_HPP

#include <ostream>
#include <boost/function.hpp>

namespace app
{
namespace http
{
struct request_progress
{
    enum enum_t
    {
        received = 0,

        put_validated,
        put_read_data,
        put_enqueued,
        put_dequeued,
        put_archived,

        response_sent,

        n_enums
    };

    static const char* to_string(const enum_t e)
    {
        const char* const strings[n_enums] = {
                "received",

                "put_validated",
                "put_read_data",
                "put_enqueued",
                "put_dequeued",
                "put_archived",

                "response_sent"
        };

        if (e >= 0 && e < n_enums) {
            return strings[e];
        }

        return "unknown";
    }
};

typedef boost::function<void(const request_progress::enum_t&)> request_progress_callback_type;

std::ostream&
operator<<(std::ostream& os, const request_progress::enum_t& e)
{
    return os << request_progress::to_string(e);
}
} // namespace http
} // namespace app

#endif //ARCHIVING_REQUEST_PROGRESS_HPP
