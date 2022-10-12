//
// Created by dkorah on 05/10/22.
//

#ifndef ARCHIVING_STORAGE_BACKENDS_HPP
#define ARCHIVING_STORAGE_BACKENDS_HPP

#include "basic_backend.hpp"
#include "s3_backend.hpp"
#include "s3_compatible_backend.hpp"

#include <boost/make_shared.hpp>

namespace {
using app::storage::basic_endpoint;
using app::storage::endpoint_type;
} // namespace

namespace archiving {
namespace archived {
namespace storage {
/**
 * A fake factory for all types of backends we support. For now it is kept simple, till we have more requirements,
 * if any, to support other kids of backends (GCP, Azure, etc)!
 *
 * NOTE: This being the place where storage backends are created, till a better way is figured out, `backends` will also
 * initialise rml::s3 module.
 */
struct backends
{
    static void describe_config(options_description& optsDesc)
    {
        // add every backend's describe_config() here
        s3_backend::describe_config(optsDesc);
        s3_compatible_backend::describe_config(optsDesc);
    }

    explicit backends(const variables_map& config)
        : s3(boost::make_shared<s3_backend>(config)), s3_compatible(boost::make_shared<s3_compatible_backend>(config))
    {
        rml::s3::initialise();
    }

    virtual ~backends() { rml::s3::deinitialise(); }

    inline boost::shared_ptr<basic_backend> get(const boost::shared_ptr<basic_endpoint>& ep) const
    {
        return get(ep->type);
    }

    inline boost::shared_ptr<basic_backend> get(endpoint_type::endpoint_type_t e) const
    {
        switch (e) {
        case endpoint_type::s3:
            return s3;
        case endpoint_type::s3_compatible:
            return s3_compatible;
        default:
            throw std::runtime_error("Endpoint-type unknown or not implemented");
        }
    }

    boost::shared_ptr<basic_backend> s3;
    boost::shared_ptr<basic_backend> s3_compatible;
};
} // namespace storage
} // namespace archived
} // namespace archiving
#endif // ARCHIVING_STORAGE_BACKENDS_HPP
