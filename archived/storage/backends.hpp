//
// Created by dkorah on 05/10/22.
//

#ifndef ARCHIVING_STORAGE_BACKENDS_HPP
#define ARCHIVING_STORAGE_BACKENDS_HPP

#include "basic_backend.hpp"
#include "libs3_backend.hpp"

#include <boost/make_shared.hpp>

namespace {
using app::storage::endpoint_type;
} // namespace

namespace archiving {
namespace archived {
namespace storage {
struct backends
{
    static void describe_config(options_description& optsDesc)
    {
        // add every backend's describe_config() here
        libs3_backend::describe_config(optsDesc);
    }

    explicit backends(const variables_map& config) : libs3(boost::make_shared<libs3_backend>(config)) {}

    inline boost::shared_ptr<basic_backend> get(endpoint_type::endpoint_type_t e) const
    {
        switch (e) {
        case endpoint_type::s3:
        case endpoint_type::s3_compatible:
            return libs3;
        default:
            throw std::runtime_error("Endpoint-type unknown or not implemented");
        }
    }

    boost::shared_ptr<libs3_backend> libs3;
};
} // namespace storage
} // namespace archived
} // namespace archiving
#endif // ARCHIVING_STORAGE_BACKENDS_HPP
