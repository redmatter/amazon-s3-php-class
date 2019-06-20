//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 19/06/2019.
//

#ifndef ARCHIVING_PUT_STATE_HPP
#define ARCHIVING_PUT_STATE_HPP

#include <iostream>

namespace common
{
namespace net
{
namespace archived
{
namespace detail
{

struct put_state
{
    enum enum_t
    {
        init,
        init_ack_waiting,
        sending_file,
        file_sent_ok,
        file_sent_ack_waiting,
        done
    };
};

std::ostream& operator<<(std::ostream& os, const put_state::enum_t e)
{
    switch (e) {
    case put_state::init:
        return os << "init";
    case put_state::init_ack_waiting:
        return os << "init_ack_waiting";
    case put_state::sending_file:
        return os << "sending_file";
    case put_state::file_sent_ok:
        return os << "file_sent_ok";
    case put_state::file_sent_ack_waiting:
        return os << "file_sent_ack_waiting";
    case put_state::done:
        return os << "done";
    }
}

} // namespace detail
} // namespace archived
} // namespace net
} // namespace common

#endif //ARCHIVING_PUT_STATE_HPP
