//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 10/01/2018.
//

#ifndef ARCHIVING_ESTUARY_APP_correlation_HPP
#define ARCHIVING_ESTUARY_APP_correlation_HPP

#include <boost/optional.hpp>
#include <boost/optional/optional_io.hpp>
#include <boost/io/ios_state.hpp>

#include <string>
#include <iostream>
#include <iomanip>

#include <stdint.h>

namespace app
{
namespace http
{
struct request_correlation
{
    const uint64_t id;
    const boost::optional<std::string> request_id;
    const boost::optional<std::string> correlation_id;

    explicit request_correlation(const uint64_t id, const boost::optional<std::string>& request_id,
                         const boost::optional<std::string>& correlation_id) :
            id(id), request_id(request_id), correlation_id(correlation_id)
    {}
};

std::ostream& operator<<(std::ostream& os, const request_correlation& rc)
{
    boost::io::ios_flags_saver ifs(os);

    os << '[' << std::hex << std::setfill('0') << std::setw(16) << rc.id << ']';

    if (rc.request_id == boost::none) {
        os << ' ';
    }
    os << rc.request_id;

    if (rc.correlation_id == boost::none) {
        os << ' ';
    }
    os << rc.correlation_id;

    return os;
}
} // namespace http
} // namespace app
#endif //ARCHIVING_ESTUARY_APP_correlation_HPP
