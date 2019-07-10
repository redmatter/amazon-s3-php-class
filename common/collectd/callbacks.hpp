//
// Created by dkorah on 05/07/17.
//

#ifndef COMMON_COLLECTD_CALLBACKS_HPP
#define COMMON_COLLECTD_CALLBACKS_HPP

#include "../archivable.hpp"

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/chrono/duration.hpp>

namespace common
{
namespace collectd
{

typedef boost::function0<void> consume_timeout_callback_type;
typedef boost::function0<void> is_finishing_callback_type;

typedef boost::function2<void, boost::shared_ptr<archiving::archivable>, bool> fn_produce_type;
typedef boost::function1<
        boost::shared_ptr<archiving::archivable>, bool> fn_consume_type;
typedef boost::function2<
        boost::shared_ptr<archiving::archivable>, bool,
        boost::chrono::milliseconds> fn_consume_with_timeout_type;
typedef boost::function4<
        boost::shared_ptr<archiving::archivable>, bool,
        boost::chrono::milliseconds,
        consume_timeout_callback_type, is_finishing_callback_type> fn_consume_with_timeout_finishing_callback_type;

typedef boost::function1<void, boost::shared_ptr<archiving::archivable> > fn_remove_from_catalogue_type;
typedef boost::function1<bool, boost::shared_ptr<archiving::archivable> > fn_is_in_catalogue_type;

typedef boost::function1<bool, boost::shared_ptr<archiving::archivable> > fn_save_to_database_type;

typedef boost::function1<void, boost::shared_ptr<archiving::archivable> > fn_on_successful_put_type;

typedef boost::function<archiving::archived_ip_t(archiving::put_group_id_t)> fn_lookup_put_group_archived_ip_type;
typedef boost::function<void(const archiving::put_group_id_t&, archiving::archived_ip_t)> fn_set_put_group_archived_ip_type;
typedef boost::function<void(const archiving::put_group_id_t&)> fn_add_put_group_type;
typedef boost::function<void(const archiving::put_group_id_t&)> fn_remove_put_group_type;

} // namespace collectd
} // namespace common

#endif //COMMON_COLLECTD_CALLBACKS_HPP
