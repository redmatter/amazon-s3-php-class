//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 12/06/2018.
//

#ifndef ARCHIVING_LEGAL_HOLD_STATUS_HPP
#define ARCHIVING_LEGAL_HOLD_STATUS_HPP

namespace app
{
struct legal_hold_status
{
    const bool purge_restricted;
    const bool access_restricted;

    legal_hold_status() : purge_restricted(false), access_restricted(false)
    {}

    legal_hold_status(bool purge_restricted, bool access_restricted)
            : purge_restricted(purge_restricted), access_restricted(access_restricted)
    {}

    legal_hold_status(const legal_hold_status& other)
            : purge_restricted(other.purge_restricted), access_restricted(other.access_restricted)
    {}
};
} // namespace app

#endif //ARCHIVING_LEGAL_HOLD_STATUS_HPP
