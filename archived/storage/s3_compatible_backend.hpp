//
// Created by dkorah on 10/10/22.
//

#ifndef ARCHIVING_STORAGE_s3_compatible_backend_HPP
#define ARCHIVING_STORAGE_s3_compatible_backend_HPP

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

struct s3_compatible_backend : public libs3_backend
{
    static void describe_config(options_description& optsDesc) {}

    explicit s3_compatible_backend(const variables_map& map) : libs3_backend() {}

    // S3-compatible backends may not support multi-delete; so we try it and fallback to single key deletion.
    virtual bool delete_multiple(const boost::shared_ptr<basic_endpoint>& ep,
                                 const delete_keys_request& keys_list,
                                 delete_keys_result& result)
    {
        // try multi-delete and check for request failure
        result = rml::s3::delete_multiple(to_bucket_context(ep), keys_list);
        if (result) {
            return result;
        }

        // if multi-delete fails, fallback to deleting one key at a time
        delete_keys_result res;
        const std::vector<std::string>& keys = keys_list.get_keys();
        typedef std::vector<std::string>::const_iterator itr;

        itr end = keys.end();
        for (itr i = keys.begin(); i != end; ++i) {
            rml::s3::result r = rml::s3::delete_(to_bucket_context(ep), *i);
            res.add_result(*i, r.status);
        }

        return res;
    }
};

} // namespace storage
} // namespace archived
} // namespace archiving

#endif // ARCHIVING_STORAGE_s3_compatible_backend_HPP
