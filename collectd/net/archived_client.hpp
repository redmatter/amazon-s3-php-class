//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 24/06/2019.
//

#ifndef ARCHIVING_COLLECTD_NET_ARCHIVED_CLIENT_HPP
#define ARCHIVING_COLLECTD_NET_ARCHIVED_CLIENT_HPP

#include "detail/retry.hpp"
#include "../../common/collectd/callbacks.hpp"
#include "../../common/net/archived/connection_manager.hpp"
#include "../../common/net/archived/send_file.hpp"

#include <rml/logger.hpp>
#include <rml/sentry.h>

#include <boost/asio/steady_timer.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>

namespace collectd
{
namespace net
{
class archived_client : public boost::enable_shared_from_this<archived_client>
{
    typedef boost::asio::basic_waitable_timer<boost::chrono::steady_clock> timer_type;

    detail::retry_manager retry_manager;

    boost::shared_ptr<common::net::archived::connection_manager> connection_manager;
    const common::collectd::fn_produce_type fn_produce;
    const common::collectd::fn_consume_with_timeout_finishing_callback_type fn_consume;
    const common::collectd::fn_remove_from_catalogue_type fn_remove_from_catalogue;
    const common::collectd::fn_save_to_database_type fn_save_to_database;
    const common::collectd::fn_on_successful_put_type fn_on_successful_put;
    const common::collectd::fn_lookup_put_group_archived_ip_type fn_lookup_put_group_archived_ip;
    const common::collectd::fn_set_put_group_archived_ip_type fn_set_put_group_archived_ip;
    const common::collectd::fn_remove_put_group_type fn_remove_put_group;

    bool shutting_down;

    void consume_timeout() const
    {
        LogVerbose("timeout consuming items to send to archived");
    }

    static archiving::put_group_id_t
    get_put_group_id(const boost::shared_ptr<archiving::archivable>& archivable)
    {
        std::string leg_uuid = archivable->get_leg_uuid();
        if (leg_uuid.empty()) {
            leg_uuid = archivable->get_uuid();
        }

        archiving::put_group_id_t ret;
        archiving::string_to_uuid(leg_uuid, ret);

        return ret;
    }

    void finalise_archivable(const boost::shared_ptr<archiving::archivable>& archivable)
    {
        fn_remove_from_catalogue(archivable);
        archivable->set_done(false);

        if (!fn_save_to_database(archivable)) {
            boost::this_thread::sleep_for(boost::chrono::seconds(2));
            if (!fn_save_to_database(archivable)) {
                LogWarning(archivable<<" Error while saving to database; giving up after retry");
            }
        }
    }

    void send_file_finished(const boost::shared_ptr<archiving::archivable>& archivable,
                            const boost::system::error_code& ec,
                            const boost::optional<boost::chrono::steady_clock::time_point>& retry_after)
    try {
        archivable->hibernate();

        if (!ec) {
            LogMessage(archivable << " finished");

            fn_remove_put_group(get_put_group_id(archivable));

            if (!fn_on_successful_put.empty()) {
                fn_on_successful_put(archivable);
            }

            finalise_archivable(archivable);

            return;
        }

        // do not retry on sure failures like file missing, which happens if purge occurs while waiting to be sent
        if (ec == common::net::archived::error::put_initialisation_failed_permanently) {
            LogMessage(archivable << " giving up; error:" << ec.message());
            fn_remove_put_group(get_put_group_id(archivable));
            fn_remove_from_catalogue(archivable);
            return;
        }

        LogMessage(archivable << " finished with error:" << ec.message());

        boost::chrono::steady_clock::time_point retry_after_;
        if (retry_after == boost::none) {
            // Always retry with a minimum of 10 seconds gap, in order to avoid a mad retry.
            retry_after_ = boost::chrono::steady_clock::now() + boost::chrono::seconds(10);
        } else {
            retry_after_ = *retry_after;
        }

        LogMessage(archivable << " rescheduling with delay");
        retry_manager.add(retry_after_, archivable);
    } catch (const std::exception& e) {
        LogWarning(archivable << ' ' << e.what());
    }

public:
    archived_client(const boost::shared_ptr<common::net::archived::connection_manager>& connection_manager,
                    const common::collectd::fn_produce_type& fn_produce,
                    const common::collectd::fn_consume_with_timeout_finishing_callback_type& fn_consume,
                    const common::collectd::fn_remove_from_catalogue_type& fn_remove_from_catalogue,
                    const common::collectd::fn_save_to_database_type& fn_save_to_database,
                    const common::collectd::fn_on_successful_put_type& fn_on_successful_put = common::collectd::fn_on_successful_put_type(),
                    const common::collectd::fn_lookup_put_group_archived_ip_type& fn_lookup_put_group_archived_ip= common::collectd::fn_lookup_put_group_archived_ip_type(),
                    const common::collectd::fn_set_put_group_archived_ip_type& fn_set_put_group_archived_ip = common::collectd::fn_set_put_group_archived_ip_type(),
                    const common::collectd::fn_remove_put_group_type& fn_remove_put_group = common::collectd::fn_remove_put_group_type()
    ) :
            connection_manager(connection_manager),
            fn_produce(fn_produce),
            fn_consume(fn_consume),
            fn_remove_from_catalogue(fn_remove_from_catalogue),
            fn_save_to_database(fn_save_to_database),
            fn_on_successful_put(fn_on_successful_put),
            fn_lookup_put_group_archived_ip(fn_lookup_put_group_archived_ip),
            fn_set_put_group_archived_ip(fn_set_put_group_archived_ip),
            fn_remove_put_group(fn_remove_put_group),

            shutting_down(false)
    {}

    void requeue_archivable(const boost::shared_ptr<archiving::archivable>& archivable) try
    {
        fn_produce(archivable, false);
    } catch (const std::exception& e) {
        LogError(archivable << " : failed to setup retry; error: " << e.what());
    }

    void scheduled_setup_archivable_retry(const boost::system::error_code& ec,
                                          const boost::shared_ptr<timer_type>& timer)
    {
        // timer was cancelled (during shutdown)
        if (ec == boost::asio::error::operation_aborted || shutting_down) {
            LogVerbose("archivable retry manager shutting down");
            return;
        }

        std::size_t count = retry_manager.schedule_batch(100, boost::bind(boost::protect(fn_produce), _1, false));
        LogMessage("rescheduled " << count << " archivable items");

        timer->expires_from_now(boost::chrono::seconds(5));
        timer->async_wait(boost::bind(&archived_client::scheduled_setup_archivable_retry,
                                      shared_from_this(),
                                      boost::asio::placeholders::error,
                                      timer));
    }

    void start_retry_scheduler(boost::asio::io_service& io_service)
    {
        LogMessage("archived_client: starting retry scheduler");
        io_service.post(boost::bind(&archived_client::scheduled_setup_archivable_retry,
                                    shared_from_this(),
                                    boost::system::error_code(),
                                    boost::make_shared<timer_type>(boost::ref(io_service))));
    }

    void run()
    {
        LogMessage("archived_client: scheduler thread starting");

        while (!shutting_down) {
            try {
                boost::shared_ptr<archiving::archivable> archivable = fn_consume(
                        false, boost::chrono::milliseconds(10000),
                        // on consume-timeout, send NOP or reconnect if connection has gone away
                        boost::protect(boost::bind(&archived_client::consume_timeout, shared_from_this())),
                        // if the queue is finishing, the archived_client has to close (shutdown) as well
                        boost::protect(boost::bind(&archived_client::shutdown, shared_from_this())));

                // if shutdown while "waiting to consume"
                if (shutting_down) {
                    break;
                }

                // if no archivable was obtained
                if (!archivable) {
                    continue;
                }

                if(archivable->get_current_state() >= archiving::archivable::state::stored) {
                    LogWarning(archivable<<" already stored; skipping");
                    finalise_archivable(archivable);
                    continue;
                }

                // make sure we don't let it fall through the cracks; if we don't succeed, item goes back into the queue.
                rml::sentry archivable_sentry(
                    boost::bind(&archived_client::requeue_archivable, shared_from_this(), archivable));

                boost::system::error_code ec;

                const archiving::put_group_id_t& put_group_id(get_put_group_id(archivable));
                uint32_t ip = fn_lookup_put_group_archived_ip(put_group_id);
                if (ip == 0) {
                    ip = connection_manager->get_active_ip_address(ec, boost::chrono::seconds(1));
                    if (ec) {
                        throw boost::system::system_error(ec);
                    }
                }

                LogVerbose(archivable<<" grabbing connection");
                boost::shared_ptr<common::net::archived::connection> conn = connection_manager->get_connection(ip, ec);
                if (ec) {
                    throw boost::system::system_error(ec);
                }

                LogVerbose(archivable<<" connection grabbed " << *conn);

                fn_set_put_group_archived_ip(put_group_id, ip);
                LogVerbose(archivable<<" IP set to " << ip);
                common::net::archived::send_file(conn, archivable,
                        boost::bind(&archived_client::send_file_finished, shared_from_this(), archivable, _1, _2),
                        boost::bind(&archived_client::is_shutting_down, shared_from_this()));

                archivable_sentry.cancel();
            } catch (const std::exception& e) {
                LogWarning(e.what());
            }
        }

        LogMessage("archived_client: scheduler thread closing");
    }

    void shutdown()
    {
        shutting_down = true;
        LogMessage("archived_client shutdown in progress");
    }

    bool is_shutting_down()
    {
        return shutting_down;
    }
};
} // namespace net
} // namespace collectd
#endif //ARCHIVING_COLLECTD_NET_ARCHIVED_CLIENT_HPP
