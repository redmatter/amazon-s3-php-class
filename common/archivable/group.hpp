//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 24/06/2019.
//

#ifndef ARCHIVING_COMMON_ARCHIVABLE_GROUP_HPP
#define ARCHIVING_COMMON_ARCHIVABLE_GROUP_HPP

#include <boost/function.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/unordered_map.hpp>

#include <map>

#include <stdint.h>

namespace common
{
namespace archivable
{

typedef boost::uuids::uuid group_id_t;
typedef uint32_t ip_t;

struct cached_ip
{
    boost::chrono::steady_clock::time_point expires_at;
    ip_t ip;

    template<class Rep, class Period>
    explicit cached_ip(ip_t ip, boost::chrono::duration<Rep, Period>& ttl) :
            expires_at(boost::chrono::steady_clock::now() + ttl), ip(ip)
    {}
};

class group_ip_cache
{
//    typedef std::map<group_id_t, cached_ip> map_type;
    typedef boost::unordered_map<group_id_t, cached_ip> map_type;

    // load-callback - called to load a group (load from database)
    typedef boost::function<ip_t(group_id_t)> load_callback_type;

    // on-add-callback - called when a new group is added (to save to database)
    typedef boost::function<void(const group_id_t&, ip_t)> on_add_callback_type;

    boost::mutex mutex;
    map_type m;
    load_callback_type load_callback;
    on_add_callback_type on_add_callback;
    boost::chrono::seconds ttl;

public:
    template<class Rep, class Period>
    group_ip_cache(const load_callback_type& load_callback, const on_add_callback_type& on_add_callback,
                   boost::chrono::duration<Rep, Period>& ttl) :
            load_callback(load_callback), on_add_callback(on_add_callback), ttl(ttl)
    {}

    void add(const std::string& leg_uuid, ip_t ip)
    {
        add(boost::lexical_cast<group_id_t>(leg_uuid), ip);
    }

    void add(const group_id_t& g, ip_t ip)
    {
        boost::unique_lock<boost::mutex> lock;
        cached_ip cip(ip, ttl);

        // insert the cached-ip
        std::pair<map_type::iterator, bool> res =
                m.insert(std::make_pair<group_id_t, cached_ip>(g, cip));

        // a new addition with 0 value for IP? We dont want to hit database with that; no value.
        // Also; we never expect the IP in cache to reset to zero.
        if (ip == 0) {
            return;
        }

        bool updated = res.second;

        // if a value already exist (usually 0) we need to replace and update database
        if (!updated && res.first->second.ip != ip) {
            m[g] = cip;
            updated = true;
        }

        if (updated) {
            on_add_callback(g, ip);
        }
    }

    ip_t get(const std::string& leg_uuid)
    {
        return get(boost::lexical_cast<group_id_t>(leg_uuid));
    }

    ip_t get(const group_id_t& g)
    {
        boost::unique_lock<boost::mutex> lock;
        map_type::iterator itr = m.find(g);
        if (itr != m.end()) {
            return itr->second.ip;
        }

        ip_t ip = load_callback(g);
        if (ip != 0) {
            m.insert(std::make_pair<group_id_t, cached_ip>(g, cached_ip(ip, ttl)));
        }

        return ip;
    }

    void prune()
    {
        boost::chrono::steady_clock::time_point now = boost::chrono::steady_clock::now();

        boost::unique_lock<boost::mutex> lock;
        map_type::iterator itr = m.begin();
        map_type::iterator end = m.end();
        while (itr != end) {
            if (itr->second.expires_at < now) {
                itr = m.erase(itr);
            } else {
                ++itr;
            }
        }
    }
};

} // namespace archivable
} // namespace common
#endif //ARCHIVING_COMMON_ARCHIVABLE_GROUP_HPP
