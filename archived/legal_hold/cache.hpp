//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 12/06/2018.
//

#ifndef ARCHIVING_LEGAL_HOLD_CACHE_HPP
#define ARCHIVING_LEGAL_HOLD_CACHE_HPP

#include <archivable.hpp>
#include "../database_internal.hpp"
#include "../database_retention.hpp"
#include "status.hpp"

namespace app
{
struct legal_hold_cache
{
    database::internal& internal_db_;
    database::retention& retention_db_;

public:
    legal_hold_cache(database::internal& internal_db, database::retention& retention_db)
            : internal_db_(internal_db), retention_db_(retention_db)
    {}

    inline legal_hold_status get_status(const archiving::archivable::identifier& id)
    {
        return get_status(retention_db_.get_retention(id));
    }

    inline legal_hold_status get_status(const retention_record::pointer& retention)
    {
        return get_status(*retention);
    }

    inline legal_hold_status get_status(const retention_record& retention)
    {
        return internal_db_.get_legal_hold_status(retention.identifier.org, retention.id);
    }
};

template <typename T>
inline bool is_purge_restricted_by_legal_hold(legal_hold_cache& cache, const T& t)
{
    return cache.get_status(t).purge_restricted;
}

} // namespace app

#endif //ARCHIVING_LEGAL_HOLD_CACHE_HPP
