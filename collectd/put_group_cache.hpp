//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 09/07/2019.
//

#ifndef ARCHIVING_APP_PUT_GROUP_CACHE_HPP
#define ARCHIVING_APP_PUT_GROUP_CACHE_HPP

#include "../common/types.hpp"
#include "put_group.hpp"

#include <rml/logger.hpp>

#include <boost/atomic.hpp>
#include <boost/chrono/system_clocks.hpp>
#include <boost/function.hpp>
#include <boost/make_shared.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/unordered_map.hpp>

namespace app {

namespace detail {

struct cached_item
{
    boost::shared_ptr<put_group> value;
    boost::chrono::steady_clock::time_point ttl;

    explicit cached_item(const boost::shared_ptr<put_group>& value)
      : value(value)
      , ttl(boost::chrono::steady_clock::now() + boost::chrono::minutes(15))
    {}
};

} // namespace detail

struct put_group_cache
{
    typedef boost::function<boost::shared_ptr<put_group>(const archiving::put_group_id_t&)> load_callback_type;
    typedef boost::function<bool(const boost::shared_ptr<put_group>&)> save_callback_type;

    put_group_cache(const load_callback_type& load_callback, const save_callback_type& save_callback) 
        : load_callback(load_callback), save_callback(save_callback) 
    {}

    archiving::archived_ip_t get(const archiving::put_group_id_t& id)
    {
        const boost::shared_ptr<put_group>& value(get_internal(id));
        return value ? value->ip : 0;
    }

    void put(const archiving::put_group_id_t& id, archiving::archived_ip_t ip)
    {
        boost::shared_ptr<put_group> value = get_internal(id);
        if (!value) {
            boost::unique_lock<boost::shared_mutex> lock(mutex);
            value = boost::make_shared<put_group>(id, 0, ip);
            m.insert(std::make_pair(id, detail::cached_item(value)));
        } else if (ip != 0 && value->ip == 0) {
            value->ip = ip;
        } else if (ip == 0) {
            value->add_ref();
        }

        try {
            if (!app::save_put_group_helper(save_callback, value)) {
                LogWarning("failed to save put_group " << id);
            }
        } catch (const std::exception& e) {
            LogWarning(e.what());
        }
    }

    void remove(const archiving::put_group_id_t& id)
    {
        const boost::shared_ptr<put_group>& value(get_internal(id));
        if (!value) {
            return;
        }

        value->sub_ref();

        try {
            if (!app::save_put_group_helper(save_callback, value)) {
                LogWarning("failed to save put_group " << id);
            }
        } catch (const std::exception& e) {
            LogWarning(e.what());
        }
    }

    void prune()
    {
        const boost::chrono::steady_clock::time_point now(boost::chrono::steady_clock::now());

        boost::unique_lock<boost::shared_mutex> lock(mutex);
        
        const map_type::iterator& end(m.end());
        for (map_type::iterator itr = m.begin(); itr != end;) {
            if (itr->second.ttl < now) {
                itr = m.erase(itr);
            } else {
                ++itr;
            }
        }
    }

private:
    typedef boost::unordered_map<archiving::put_group_id_t, detail::cached_item> map_type;

    boost::shared_ptr<put_group> get_internal(const archiving::put_group_id_t& id)
    {
        boost::upgrade_lock<boost::shared_mutex> lock(mutex);

        map_type::const_iterator itr = m.find(id);
        const bool found = itr != m.end();
        if (found && itr->second.ttl > boost::chrono::steady_clock::now()) {
            return itr->second.value;
        }

        boost::shared_ptr<put_group> value;

        try {
            value = load_callback(id);

            // if we couldn't load anything
            if (!value) {
                // and we had found a value in cache, return that
                if (found) {
                    return itr->second.value;
                }

                // else return nil
                return value;
            }

            boost::upgrade_to_unique_lock<boost::shared_mutex> unique_lock(lock);

            if (found) {
                m.erase(itr);
            }

            m.insert(std::make_pair(id, detail::cached_item(value)));

            return value;
        } catch (const std::exception& e) {
            LogError(e.what());
        }

        // fallback if load_callback throws
        if (found) {
            return itr->second.value;
        }

        return value;
    }

    map_type m;
    boost::shared_mutex mutex;

    const load_callback_type load_callback;
    const save_callback_type save_callback;
};

} // namespace app

#endif // ARCHIVING_APP_PUT_GROUP_CACHE_HPP
