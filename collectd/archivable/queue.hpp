//
// Created by dkorah on 27/06/17.
//

#ifndef COLLECTD_ARCHIVABLE_QUEUE_HPP
#define COLLECTD_ARCHIVABLE_QUEUE_HPP

#include "../../common/archivable.hpp"
#include "../../common/collectd/callbacks.hpp"
#include "utils.hpp"
#include "error.hpp"
#include "catalogue.hpp"

#include <rml/queue/queue.hpp>

namespace collectd
{
namespace archivable
{

namespace rmlq = rml::queue;

typedef rmlq::queue<
        boost::shared_ptr<archiving::archivable>,
        rmlq::detail::shared_ptr_hash<boost::shared_ptr<archiving::archivable> >,
        rmlq::detail::shared_ptr_equal_to<boost::shared_ptr<archiving::archivable> > > base_queue;

base_queue::classifier_type::classification_type classify(const boost::shared_ptr<archiving::archivable> &p)
{
    switch (p->get_type()) {
        case archiving::file_type::cdr:
            return 0;
        case archiving::file_type::buffered_recording:
            return 1;
        case archiving::file_type::recording:
            return 2;
        default:
            return 3;
    }
}

boost::chrono::nanoseconds calculate_deadline(const boost::shared_ptr<archiving::archivable> &p)
{
    using namespace boost::chrono;
    return duration_cast<nanoseconds>(seconds(
            10 // everything gets a deadline at least 10s in the future
            + classify(p) * 10 // each class is 10s apart
            + p->get_attempt() * 300)); // each retry is further 5mins apart
}

struct queue : public base_queue
{
    queue(const std::string &name, const boost::shared_ptr<catalogue> &catalogue) :
            base_queue(boost::bind(classify, _1), boost::bind(calculate_deadline, _1), name), catalogue_(catalogue)
    {}

    void produce(const boost::shared_ptr<archiving::archivable> &item, const boost::chrono::milliseconds &wait_timeout,
                 const bool add_to_catalogue)
    {
        boost::system::error_condition ec;
        if (add_to_catalogue) {
            catalogue::item_sentry sentry(catalogue_, item);
            base_queue::produce(item, wait_timeout, ec);
            if (!ec) {
                sentry.cancel();
            }
        } else {
            base_queue::produce(item, wait_timeout, ec);
        }

        if (ec) {
            throw boost::system::system_error(ec.value(), ec.category());
        }
    }

    boost::shared_ptr<archiving::archivable>
    consume(const boost::chrono::milliseconds &wait_timeout, const bool remove_from_catalogue)
    {
        boost::system::error_condition ec;
        boost::shared_ptr<archiving::archivable> item;

        base_queue::consume(item, wait_timeout, ec);
        if (!ec) {
            item->set_next_attempt();
            if (remove_from_catalogue) {
                this->remove_from_catalogue(item);
            }
        }

        if (ec) {
            throw boost::system::system_error(ec.value(), ec.category());
        }

        return item;
    }

    boost::shared_ptr<archiving::archivable>
    consume(const boost::chrono::milliseconds &wait_timeout, const bool remove_from_catalogue,
            const common::collectd::consume_timeout_callback_type &timeout_callback,
            const common::collectd::is_finishing_callback_type &is_finishing_callback)
    {
        try {
            return consume(wait_timeout, remove_from_catalogue);
        } catch (const boost::system::system_error &e) {
            if (e.code() == rmlq::error::make_error_condition(rmlq::error::runtime_errors::consume_timeout)) {
                LogVerbose(*this<<" consume timeout");
                timeout_callback();
                return boost::shared_ptr<archiving::archivable>();
            } else if (e.code() == rmlq::error::make_error_condition(rmlq::error::runtime_errors::is_finishing)) {
                LogMessage(*this<<" finishing");
                is_finishing_callback();
                return boost::shared_ptr<archiving::archivable>();
            } else {
                LogVerbose(*this<<" consume:error "<<e.what());
                throw;
            }
        } catch (...) {
            throw;
        }
    }

    inline void remove_from_catalogue(const boost::shared_ptr<archiving::archivable> &item)
    {
        catalogue_->remove(item);
    }

private:
    boost::shared_ptr<catalogue> catalogue_;
};

}
}

#endif //COLLECTD_ARCHIVABLE_QUEUE_HPP
