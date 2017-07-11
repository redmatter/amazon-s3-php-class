//
// Created by dkorah on 10/07/17.
//

#ifndef COLLECTSMSD_ARCHIVABLE_CHANNEL_HPP
#define COLLECTSMSD_ARCHIVABLE_CHANNEL_HPP

#include "../../common/archivable.hpp"
#include "../../common/collectd/callbacks.hpp"

namespace collectsmsd
{
namespace archivable
{
struct channel
{
    typedef boost::shared_ptr<archiving::archivable> item_type;
    typedef common::collectd::consume_timeout_callback_type consume_timeout_callback_type;

    archiving::archivable_channel_t& channel_;
    archiving::archivable_busy_set_t& busy_set_;

    channel(archiving::archivable_channel_t &channel, archiving::archivable_busy_set_t &busy_set)
            : channel_(channel), busy_set_(busy_set)
    {}

    /**
     * Produce into the wrapped queue
     *
     * NOTE: sp_delay to be specialised at bind()
     *
     * @param item
     * @param add_to_catalogue
     * @param sp_delay
     * @return
     */
    void produce(const item_type &item, const bool& add_to_catalogue, unsigned int sp_delay)
    {
        if (add_to_catalogue) {
            busy_set_.add(item);
        }
        channel_.produce(item, sp_delay);
    }

    /**
     * Remove item from the wrapped busyset
     *
     * @param item
     */
    inline void remove_from_busyset(const item_type &item)
    {
        busy_set_.remove(item);
    }

    /**
     * Consume from the wrapped channel
     *
     * @param remove_from_busyset
     * @param wait_timeout
     * @param timeout_callback
     * @return
     */
    item_type consume(const bool &remove_from_busyset, const boost::chrono::milliseconds &wait_timeout,
                      const consume_timeout_callback_type &timeout_callback)
    {
        item_type ret;

        namespace chrono = boost::chrono;
        if (wait_timeout != chrono::milliseconds::zero()) {
            const chrono::seconds& wait_s(chrono::duration_cast<chrono::seconds>(wait_timeout));
            const chrono::microseconds &wait_us(chrono::duration_cast<chrono::microseconds>(wait_timeout) -
                                                chrono::microseconds(wait_s * 1000000));
            try {
                ret = channel_.consume(wait_s.count(), wait_us.count());
            } catch (archiving::archivable_channel_t::consume_timed_out_exception& e) {
                if (timeout_callback.empty()) {
                    throw;
                }

                timeout_callback();
            }
        } else {
            ret = channel_.consume();
        }

        if (ret && remove_from_busyset) {
            channel::remove_from_busyset(ret);
        }

        return ret;
    }
};

}
}

#endif //COLLECTSMSD_ARCHIVABLE_CHANNEL_HPP
