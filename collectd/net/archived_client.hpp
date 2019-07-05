//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 24/06/2019.
//

#ifndef ARCHIVING_COLLECTD_NET_ARCHIVED_CLIENT_HPP
#define ARCHIVING_COLLECTD_NET_ARCHIVED_CLIENT_HPP

#include "../../common/collectd/callbacks.hpp"
#include "../../common/net/archived/connection_manager.hpp"
#include "../../common/net/archived/send_file.hpp"

#include <rml/logger.hpp>

#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>

namespace collectd
{
namespace net
{
class archived_client : public boost::enable_shared_from_this<archived_client>
{
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

    void send_file_finished(const boost::shared_ptr<archiving::archivable>& archivable, const boost::system::error_code& ec)
    {
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

        LogMessage(archivable << " finished with error:" << ec.message());
        fn_produce(archivable, false /* already in catalogue */);
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

    void run()
    {
        while (!shutting_down) {
            boost::shared_ptr<archiving::archivable> archivable;
            try {
                archivable.reset();

                archivable = fn_consume(
                        false, boost::chrono::milliseconds(10000),
                        // on consume-timeout, send NOP or reconnect if connection has gone away
                        boost::protect(boost::bind(&archived_client::consume_timeout, shared_from_this())),
                        // if the queue is finishing, the archived_client has to close (shutdown) as well
                        boost::protect(boost::bind(&archived_client::shutdown, shared_from_this())));

                // if no archivable was obtained
                if (!archivable) {
                    continue;
                }

                if(archivable->get_current_state() >= archiving::archivable::state::stored) {
                    LogWarning(archivable<<" already stored; skipping");
                    finalise_archivable(archivable);
                    continue;
                }

                boost::system::error_code ec;

                const archiving::put_group_id_t& put_group_id(get_put_group_id(archivable));
                uint32_t ip = fn_lookup_put_group_archived_ip(put_group_id);
                if (ip == 0) {
                    ip = connection_manager->get_active_ip_address(ec, boost::chrono::seconds(1));
                    if (ec) {
                        throw std::runtime_error(ec.message());
                    }
                }

                LogVerbose(archivable<<" grabbing connection");
                boost::shared_ptr<common::net::archived::connection> conn = connection_manager->get_connection(ip, ec);
                if (ec) {
                    throw std::runtime_error(ec.message());
                }

                LogVerbose(archivable<<" connection grabbed " << *conn);

                fn_set_put_group_archived_ip(put_group_id, ip);
                common::net::archived::send_file(conn, archivable,
                        boost::bind(&archived_client::send_file_finished, shared_from_this(), archivable, _1));
            } catch (const std::exception& e) {
                LogWarning(e.what());
                if (archivable) {
                    try {
                        fn_produce(archivable, false /* already in the catalogue */);
                    } catch (const std::exception& e) {
                        LogError(archivable << " : failed to setup retry" << e.what());
                    }
                }
            }
        }
    }

    void shutdown()
    {
        shutting_down = true;
    }
};
} // namespace net
} // namespace collectd
#endif //ARCHIVING_COLLECTD_NET_ARCHIVED_CLIENT_HPP
