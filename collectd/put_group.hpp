//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 09/07/2019.
//

#ifndef ARCHIVING_APP_PUT_GROUP_HPP
#define ARCHIVING_APP_PUT_GROUP_HPP

#include "../common/types.hpp"

#include <boost/atomic.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>

namespace app {

struct put_group
{
    archiving::put_group_id_t id;
    boost::atomic_uint ref_count;
    archiving::archived_ip_t ip;

    boost::atomic_bool saving;

    put_group(const archiving::put_group_id_t& id, uint ref_count, archiving::archived_ip_t ip)
      : id(id)
      , ref_count(ref_count)
      , ip(ip)
    {}

    inline void add_ref() { ref_count.fetch_add(1, boost::memory_order_relaxed); }
    inline void sub_ref() { ref_count.fetch_sub(1, boost::memory_order_relaxed); }

    inline void set_saving() { saving = true; }
    inline bool is_saving() { return saving.load(); }
    inline void set_saved() { saving = false; }
};

namespace detail {

struct mark_put_group_saved
{
    boost::shared_ptr<put_group> pg;

    void operator()(put_group*) { pg->set_saved(); }

    explicit mark_put_group_saved(const boost::shared_ptr<put_group>& pg) : pg(pg)
    {}
};

boost::shared_ptr<put_group>
make_saving_put_group(const boost::shared_ptr<put_group>& pg)
{
    pg->set_saving();
    return boost::shared_ptr<put_group>(pg.get(), mark_put_group_saved(pg));
}

} // namespace detail

bool
save_put_group_helper(const boost::function<bool(const boost::shared_ptr<put_group>&)>& callback,
                      const boost::shared_ptr<put_group>& pg)
{
    // if already saving, do nothing
    if (pg->is_saving()) {
        return true;
    }

    return callback(detail::make_saving_put_group(pg));
}

} // namespace app

#endif // ARCHIVING_APP_PUT_GROUP_CACHE_HPP
