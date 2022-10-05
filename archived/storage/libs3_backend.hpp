//
// Created by dkorah on 05/10/22.
//

#ifndef ARCHIVING_STORAGE_LIBS3_BACKEND_HPP
#define ARCHIVING_STORAGE_LIBS3_BACKEND_HPP

#include "basic_backend.hpp"

#include <rml/s3.hpp>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>

namespace {

using app::types::data_sub_format;
using app::types::file_format;
using app::types::metadata;
using boost::program_options::options_description;
using boost::program_options::value;
using boost::program_options::variables_map;

} // namespace

namespace archiving {
namespace archived {
namespace storage {
struct libs3_backend : public basic_backend
{
    static void describe_config(options_description& optsDesc) {}

    explicit libs3_backend(const variables_map& map) {}

    bool put(const operation_data::pointer& opdata)
    {
        rml::utils::tmp_fstream::pointer tmpfs =
            opdata->get_tmp_fstream(opdata->p_detail->target_file_format, false, false);
        if (!tmpfs) {
            THROW_RML_EXCEPTION(opdata->p_detail->identifier << ": No target format found in opdata");
        }

        // ensure that the file-pointer is at the beginning
        tmpfs->re_open();

        const metadata::pointer& p_metadata(opdata->p_detail->p_metadata);

        rml::s3::caseless_key_string_map headers;
        p_metadata->make_headers(headers);

        std::string leg_uuid;
        {
            metadata::const_iterator itr = p_metadata->find("LegUUID");
            if (itr != p_metadata->end()) {
                leg_uuid = itr->second;
            }
        }

#define PadZeroTwoDigit(num) ((num <= 9 ? "0" : "") + boost::lexical_cast<std::string>(num))

#define YmdToString(ymd)                                                                                               \
    boost::lexical_cast<std::string>(ymd.year) + '-' + PadZeroTwoDigit(ymd.month.as_number()) + '-' +                  \
        PadZeroTwoDigit(ymd.day)

        headers
            .add("file-type-is-compressed",
                 file_format::is_compressed(opdata->p_detail->target_file_format) ? "true" : "false")
            .add("file-type-is-encrypted",
                 file_format::is_encrypted(opdata->p_detail->target_file_format) ? "true" : "false")
            .add("file-type",
                 data_sub_format::to_string(
                     data_sub_format::data_sub_format_t(opdata->p_detail->target_file_format & 0xff)))

            .add("clear-size", boost::lexical_cast<std::string>(opdata->p_detail->clear_size))
            .add("real-size", boost::lexical_cast<std::string>(opdata->p_detail->real_size))

            .add("creation-date", YmdToString(opdata->p_detail->policy.get_creation_ymd()))
            .add("retention-min-date", YmdToString(opdata->p_detail->policy.get_min_retention_ymd()))
            .add("retention-max-date", YmdToString(opdata->p_detail->policy.get_max_retention_ymd()))

            .add("policy-id", boost::lexical_cast<std::string>(opdata->p_detail->policy.p_policy->id))
            .add("policy-version", boost::lexical_cast<std::string>(opdata->p_detail->policy.p_policy->version))

            .add("leg-uuid", leg_uuid);

        return rml::s3::put(to_bucket_context(opdata->p_detail->p_endpoint),
                            std::string(opdata->p_detail->identifier),
                            *tmpfs,
                            headers,
                            boost::chrono::steady_clock::time_point(),
                            "",
                            "",
                            S3CannedAclBucketOwnerRead);
    }

private:
    static inline S3BucketContext to_bucket_context(const boost::shared_ptr<app::storage::basic_endpoint>& endpoint)
    {
        S3BucketContext bucket_context;

        bucket_context.securityToken = 0;

        switch (endpoint->type) {
        case app::storage::endpoint_type::s3: {
            namespace ns = app::storage::s3;
            boost::shared_ptr<ns::endpoint> s3e = boost::static_pointer_cast<ns::endpoint>(endpoint);
            bucket_context.protocol = S3ProtocolHTTPS;
            bucket_context.uriStyle = S3UriStyleVirtualHost;
            bucket_context.bucketName = &s3e->bucket[0];
            bucket_context.accessKeyId = &s3e->access_key[0];
            bucket_context.secretAccessKey = &s3e->secret_key[0];
            bucket_context.hostName = 0;
            bucket_context.authRegion = &s3e->region[0];
            break;
        }
        case app::storage::endpoint_type::s3_compatible: {
            namespace ns = app::storage::s3_compatible;
            boost::shared_ptr<ns::endpoint> s3e = boost::static_pointer_cast<ns::endpoint>(endpoint);
            bucket_context.protocol = S3ProtocolHTTP;
            bucket_context.uriStyle = S3UriStylePath;
            bucket_context.bucketName = &s3e->bucket[0];
            bucket_context.accessKeyId = &s3e->access_key[0];
            bucket_context.secretAccessKey = &s3e->secret_key[0];
            bucket_context.hostName = &s3e->host[0];
            bucket_context.authRegion = &s3e->region[0];
            break;
        }
        default:
            throw std::runtime_error("not an s3 or s3-compatible endpoint");
        }

        return bucket_context;
    }
};
} // namespace storage
} // namespace archived
} // namespace archiving

#endif // ARCHIVING_STORAGE_LIBS3_BACKEND_HPP
