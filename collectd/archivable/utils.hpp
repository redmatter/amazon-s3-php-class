//
// Created by dkorah on 04/07/17.
//

#ifndef COLLECTD_ARCHIVABLE_UTILS_HPP
#define COLLECTD_ARCHIVABLE_UTILS_HPP

#include "../../common/archivable.hpp"

namespace archiving {

std::size_t hash_value(archivable const &a)
{
    boost::hash<std::string> hasher;
    return hasher(a.get_uuid());
}

bool operator==(const archivable &x, const archivable &y)
{
    return x.get_type() == y.get_type() &&
           x.get_org_id() == y.get_org_id() &&
           x.get_uuid() == y.get_uuid();
}

bool operator<(const archivable &x, const archivable &y)
{
    const archiving::file_type::file_type_t& x_file_type(x.get_type());
    const archiving::file_type::file_type_t& y_file_type(y.get_type());

    if (x_file_type != y_file_type) {
        return x_file_type < y_file_type;
    }

    return x.get_uuid() < y.get_uuid();
}

}

#endif //COLLECTD_ARCHIVABLE_UTILS_HPP
