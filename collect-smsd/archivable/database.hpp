//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 19/07/17.
//

#ifndef COLLECTSMSD_ARCHIVABLE_DATABASE_HPP
#define COLLECTSMSD_ARCHIVABLE_DATABASE_HPP

#include "../../common/collectd/callbacks.hpp"

#include <boost/thread/recursive_mutex.hpp>
#include <boost/chrono/duration.hpp>
#include <boost/thread.hpp>

namespace collectsmsd
{
namespace archivable
{

struct database : private boost::noncopyable
{
    typedef std::list<archiving::archivable::pointer> archivable_list;

    explicit database(const common::collectd::fn_save_to_database_type &fn_save_real,
                      const boost::chrono::seconds &retry_interval) :
            fn_save_real_(fn_save_real), retry_interval_(retry_interval)
    {}

    bool save(const archiving::archivable::pointer &archivable) try
    {
        const archiving::archivable::pointer& p_parent(archivable->get_parent());
        if(p_parent && !p_parent->get_database_id()) {
            (void) save(p_parent);
        }

        if (!fn_save_real_(archivable)) {
            boost::recursive_mutex::scoped_lock lock(save_list_mutex_);

            // check if save already in progress from an earlier call to save()
            if(!archivable->set_save_setup()) {
                save_list_.push_back(archivable);
            }
        }

        return true;
    }
    catch(const std::exception& e) {
        LogError(*archivable<<" failed to save; exception:"<<e.what());
        return false;
    } catch(...) {
        LogError(*archivable<<" failed to save; exception:unknown");
        return false;
    }

    void save_run(volatile bool& quit) try
    {
        boost::chrono::steady_clock::time_point sleep_until(boost::chrono::steady_clock::now() + retry_interval_);
        while(!quit) {
            if(boost::chrono::steady_clock::now() < sleep_until) {
                boost::this_thread::sleep_for(boost::chrono::seconds(1));
                continue;
            }

            save_all();

            sleep_until = boost::chrono::steady_clock::now() + retry_interval_;
        }

        // any pending shall be saved when we quit
        save_all();
    }
    catch(const std::exception& e) {
        LogError("save_run failed; exception:"<<e.what());
    }
    catch(...) {
        LogError("save_run failed; exception:unknown");
    }

private:
    inline void save_all() try
    {
        boost::recursive_mutex::scoped_lock lock(save_list_mutex_);

        if(save_list_.empty())
            return;

        try {
            for(archivable_list::iterator itr = save_list_.begin(); itr != save_list_.end(); ) {
                archiving::archivable::pointer archivable = *itr;
                if(!fn_save_real_(archivable)) {
                    LogVerbose(*archivable<<" failed to save; will try again later");
                    ++itr;
                    continue;
                }

                archivable->reset_save_setup();

                save_list_.erase(itr++);
            }
        }
        catch(const std::exception& e) {
            LogWarning(e.what());
        }
    }
    catch(const std::exception& e) {
        LogError("save_all failed; exception:"<<e.what());
    }
    catch(...) {
        LogError("save_all failed; exception:unknown");
    }

    const common::collectd::fn_save_to_database_type &fn_save_real_;
    const boost::chrono::seconds retry_interval_;
    archivable_list save_list_;
    boost::recursive_mutex save_list_mutex_;
};

} // namespace archivable
} // namespace collectsmsd

#endif //COLLECTSMSD_ARCHIVABLE_DATABASE_HPP
