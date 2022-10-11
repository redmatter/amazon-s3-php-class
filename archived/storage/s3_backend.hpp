//
// Created by dkorah on 10/10/22.
//

#ifndef ARCHIVING_STORAGE_S3_BACKEND_HPP
#define ARCHIVING_STORAGE_S3_BACKEND_HPP

#include "libs3_backend.hpp"

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>

namespace {
using boost::program_options::options_description;
using boost::program_options::variables_map;
} // namespace

namespace archiving {
namespace archived {
namespace storage {

struct s3_backend : public libs3_backend
{
    static void describe_config(options_description& optsDesc) {}

    explicit s3_backend(const variables_map& map) : libs3_backend() {}

    // S3 supports multi-delete
    virtual bool delete_multiple(const boost::shared_ptr<basic_endpoint>& ep,
                                 const delete_keys_request& keys_list,
                                 delete_keys_result& result)
    {
        result = rml::s3::delete_multiple(to_bucket_context(ep), keys_list);
        return result;
    }
};

} // namespace storage
} // namespace archived
} // namespace archiving

#endif // ARCHIVING_STORAGE_S3_BACKEND_HPP
