//
// Created by dkorah on 05/10/22.
//

#ifndef ARCHIVING_STORAGE_SERVICE_HPP
#define ARCHIVING_STORAGE_SERVICE_HPP

#include "../database_internal.hpp"
#include "../monitor.hpp"
#include "../operation_data.hpp"
#include "backends.hpp"

#include <rml/logger.hpp>

#include <boost/make_shared.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/thread.hpp>

namespace {

using app::operation_data;
using app::operation_outcome;
using app::operation_state;
using app::storage::endpoint_type;
using boost::program_options::options_description;
using boost::program_options::value;
using boost::program_options::variables_map;

enum
{
    min_threads = 2
};

} // namespace

namespace archiving {
namespace archived {
namespace storage {

struct service : boost::enable_shared_from_this<service>
{
    static void describe_config(options_description& optsDesc)
    {
        optsDesc.add_options()(
            "storage_service.threads", value<unsigned>()->default_value(min_threads), "storage service threads");
        libs3_backend::describe_config(optsDesc);
    }

    service(const variables_map& config,
            operation_data::channel_t& in_channel,
            operation_data::channel_t& out_channel,
            app::storage::cached_endpoint_finder_t& cached_endpoint_finder,
            app::database::internal& internal_db,
            const backends& b)
        : running(false)
        , config(config)
        , in_channel(in_channel)
        , out_channel(out_channel)
        , cached_endpoint_finder(cached_endpoint_finder)
        , internal_db(internal_db)
        , backends_(b)
    {
    }

    /**
     * Start storage service threads
     */
    void start()
    {
        unsigned n_threads = std::max<unsigned>(min_threads, config["storage_service.threads"].as<unsigned>());
        LogMessage("Starting "<<n_threads<<" storage-service threads");
        while (n_threads--) {
            threads.create_thread(boost::bind(&service::run, shared_from_this()));
        }
    }

    /**
     * Stop and wait for all threads to finish
     */
    void stop()
    {
        if (!running) {
            return;
        }

        running = false;
        threads.interrupt_all();
        threads.join_all();
    }

private:
    /**
     * Calculate retry interval, increasing exponentially; 3 min, 12 mins, 27 mins, etc. After 5 attempts we retry
     * every 2 hours.
     */
    static inline int get_retry_delay(const operation_data::pointer& opdata)
    {
        return (opdata->p_detail->retry > 5) ? 7200 : (180 * opdata->p_detail->retry * opdata->p_detail->retry);
    }

    inline void requeue(operation_data::pointer& opdata)
    {
        if (!running) {
            return;
        }

        if (!opdata) {
            return;
        }

        opdata->hibernate();

        LogMessage(opdata->p_detail->identifier << ": setting up retry");

        opdata->p_detail->retry++;
        opdata->p_detail->state = operation_state::put_file_convert_ok; // setup retry

        internal_db.save(opdata); // save before we potentially hand off to another thread

        // Reset the endpoint we looked up so that next retry will be forced to lookup again. This allows for
        // any configuration changes to take effect.
        opdata->p_detail->p_endpoint.reset();

        bool ret = false;
        pc_produce_get_result(ret, in_channel, opdata, get_retry_delay(opdata), 0, 0);
        if (!ret) {
            LogError(opdata->p_detail->identifier << ": setup of retry failed");
        }

        opdata.reset();
    }

    inline void move_forward(operation_data::pointer& opdata, operation_state::operation_state_t next_state)
    {
        opdata->hibernate();

        opdata->p_detail->state = next_state; // operation_state::make(opdata->p_detail->state, operation_outcome::ok);
        internal_db.save(opdata);             // save before we potentially hand off to another thread

        // get into a safe state before allowing the current thread to stop
        if (!running) {
            return;
        }

        pc_produce(out_channel, opdata);

        opdata.reset();

        app::monitor::increment_stored();
        app::monitor::increment_stored_size(opdata->p_detail->real_size);
    }

    void run()
    {
        while (!boost::this_thread::interruption_requested()) {
            operation_data::pointer opdata;
            try {
                opdata = in_channel.consume();

                if (opdata->get_operation() != app::operation::put_object) {
                    LogCritical(opdata->p_detail->identifier << ": Operation not implemented");
                    continue;
                }

                if ((operation_state::put_file_save_ok == opdata->p_detail->state) ||
                    (((opdata->p_detail->state & 0x00f0) >> 4) > app::operation_coarse::file_save)) {

                    LogVerbose(opdata->p_detail->identifier << " already saved; shifting to next channel.");
                    move_forward(opdata, opdata->p_detail->state); // move forward, retaining the current state
                    continue;
                }

                if (!opdata->p_detail->p_endpoint) {
                    const std::pair<archiving::org_id_t, archiving::endpoint_id_t>& lookup(
                        std::make_pair<archiving::org_id_t, archiving::endpoint_id_t>(
                            opdata->p_detail->identifier.org, opdata->p_detail->policy.p_policy->endpoint_id));
                    if (!(opdata->p_detail->p_endpoint = cached_endpoint_finder(lookup))) {
                        cached_endpoint_finder.forget(lookup);
                        LogWarning(opdata->p_detail->identifier << ": Endpoint not found");
                        requeue(opdata);
                        continue;
                    }
                }

                // Get the backend and PUT data
                if (!backends_.get(opdata->p_detail->p_endpoint)->put(opdata)) {
                    LogWarning(opdata->p_detail->identifier << ": Failed to store");
                    requeue(opdata);
                    continue;
                }

                LogMessage(opdata->p_detail->identifier << ": Saved to storage; queueing for retention");
                move_forward(opdata, operation_state::put_file_save_ok);
            } catch (const operation_data::channel_t::finishing_exception& e) {
                LogMessage("Channel(s) finishing");
                break;
            } catch (const std::exception& e) {
                LogError("exception: " << e.what());
                requeue(opdata);
            } catch (...) {
                LogError("Unexpected exception");
                requeue(opdata);
            }
        }
    }

    bool running;
    boost::thread_group threads;
    const variables_map& config;
    operation_data::channel_t& in_channel;
    operation_data::channel_t& out_channel;
    app::storage::cached_endpoint_finder_t& cached_endpoint_finder;
    app::database::internal& internal_db;
    const backends& backends_;
};

} // namespace storage
} // namespace archived
} // namespace archiving

#endif // ARCHIVING_STORAGE_SERVICE_HPP
