//
// Created by dkorah on 10/07/17.
//

#ifndef COLLECTITEMD_ARCHIVABLE_CHANNEL_HPP
#define COLLECTITEMD_ARCHIVABLE_CHANNEL_HPP

#include "../../common/archivable.hpp"
#include "../../common/collectd/callbacks.hpp"

namespace collectimportd
{
namespace archivable
{
struct channel
{
    typedef boost::shared_ptr<archiving::archivable> item_type;
    typedef common::collectd::consume_timeout_callback_type consume_timeout_callback_type;
    typedef common::collectd::is_finishing_callback_type is_finishing_callback_type;

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
                      const consume_timeout_callback_type &timeout_callback,
                      const is_finishing_callback_type &is_finishing_callback)
    {
        item_type ret;

        try {
            if (wait_timeout != boost::chrono::milliseconds::zero()) {
                ret = channel_.consume(/* timeout_seconds */ unsigned(wait_timeout.count() / 1000),
                                       /* timeout_microseconds */ unsigned((wait_timeout.count() % 1000) * 1000));
            } else {
                ret = channel_.consume();
            }
        } catch (archiving::archivable_channel_t::consume_timed_out_exception& e) {
            // consume_timed_out_exception is thrown only when wait_timeout != zero()
            if (timeout_callback.empty()) {
                throw;
            }

            LogVerbose(channel_<<" consume timeout");
            timeout_callback();
        } catch (archiving::archivable_channel_t::finishing_exception& e) {
            if (is_finishing_callback.empty()) {
                throw;
            }

            LogMessage(channel_<<" finishing");
            is_finishing_callback();
        }

        if (ret && remove_from_busyset) {
            channel::remove_from_busyset(ret);
        }

        return ret;
    }
};

}
}

#endif //COLLECTITEMD_ARCHIVABLE_CHANNEL_HPP
