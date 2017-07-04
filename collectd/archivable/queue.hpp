//
// Created by dkorah on 27/06/17.
//

#ifndef COLLECTD_ARCHIVABLE_QUEUE_HPP
#define COLLECTD_ARCHIVABLE_QUEUE_HPP

#include "../../common/archivable.hpp"
#include "utils.hpp"

#include <rml/queue/queue.hpp>

namespace collectd {
namespace archivable {

typedef rml::queue::queue<
        archiving::archivable::pointer,
        rml::queue::detail::shared_ptr_hash<archiving::archivable::pointer>,
        rml::queue::detail::shared_ptr_equal_to<archiving::archivable::pointer> > base_queue;

struct queue : public base_queue
{
    queue(const fn_classify_type &fn_classify, const fn_calculate_deadline_type &fn_calculate_deadline,
          const std::string &name) : base_queue(fn_classify, fn_calculate_deadline, name)
    {}
};

}
}

#endif //COLLECTD_ARCHIVABLE_QUEUE_HPP
