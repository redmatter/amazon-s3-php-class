//
// Created by dkorah on 05/10/22.
//

#ifndef ARCHIVING_STORAGE_LIBS3_BACKEND_HPP
#define ARCHIVING_STORAGE_LIBS3_BACKEND_HPP

#include "basic_backend.hpp"

#include <rml/s3.hpp>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>

namespace {

using app::types::data_sub_format;
using app::types::metadata;
using boost::posix_time::ptime;

inline std::string
to_string(const ptime::date_type::ymd_type& d)
{
    const ptime::date_type::ymd_type::year_type::value_type yyyy(d.year);
    const ptime::date_type::ymd_type::month_type::value_type mm(d.month.as_number());
    const ptime::date_type::ymd_type::day_type::value_type dd(d.day);

    std::string buf(11, ' ');
    std::snprintf(&buf[0], buf.size(), "%4d-%02d-%02d", yyyy, mm, dd);
    return buf;
}

inline const char*
to_string(bool v)
{
    return (v ? "true" : "false");
}

inline const char*
to_string(data_sub_format::data_sub_format_t dsf)
{
    return data_sub_format::to_string(dsf);
}

inline data_sub_format::data_sub_format_t
to_data_sub_format(app::types::file_format::file_format_t f)
{
    return static_cast<data_sub_format::data_sub_format_t>(f & 0xff);
}

} // namespace

namespace archiving {
namespace archived {
namespace storage {

struct libs3_backend : public basic_backend
{
    libs3_backend() { rml::s3::initialise(); }
    virtual ~libs3_backend() { rml::s3::deinitialise(); }

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

        headers_list headers;
        p_metadata->fill_headers(boost::bind(&headers_list::append, boost::ref(headers), _1, _2));

        std::string leg_uuid;
        {
            metadata::const_iterator itr = p_metadata->find("LegUUID");
            if (itr != p_metadata->end()) {
                leg_uuid = itr->second;
            }
        }

        headers
            .append("file-type-is-compressed",
                    to_string(app::types::file_format::is_compressed(opdata->p_detail->target_file_format)))
            .append("file-type-is-encrypted",
                    to_string(app::types::file_format::is_encrypted(opdata->p_detail->target_file_format)))
            .append("file-type", to_string(to_data_sub_format(opdata->p_detail->target_file_format)))

            .append("clear-size", boost::lexical_cast<std::string>(opdata->p_detail->clear_size))
            .append("real-size", boost::lexical_cast<std::string>(opdata->p_detail->real_size))

            .append("creation-date", to_string(opdata->p_detail->policy.get_creation_ymd()))
            .append("retention-min-date", to_string(opdata->p_detail->policy.get_min_retention_ymd()))
            .append("retention-max-date", to_string(opdata->p_detail->policy.get_max_retention_ymd()))

            .append("policy-id", boost::lexical_cast<std::string>(opdata->p_detail->policy.p_policy->id))
            .append("policy-version", boost::lexical_cast<std::string>(opdata->p_detail->policy.p_policy->version))

            .append("leg-uuid", leg_uuid);

        LogVerbose(opdata->p_detail->identifier << ": headers: " << headers);

        return rml::s3::put(to_bucket_context(opdata->p_detail->p_endpoint),
                            std::string(opdata->p_detail->identifier),
                            *tmpfs,
                            headers,
                            boost::chrono::steady_clock::time_point(),
                            "",
                            "",
                            S3CannedAclBucketOwnerRead);
    }

    bool get(const operation_data::pointer& opdata, headers_list& headers)
    {
        rml::utils::tmp_fstream::pointer ofs =
            opdata->get_tmp_fstream(opdata->p_detail->current_file_format, true, true);

        rml::s3::response_properties::pointer res = rml::s3::get(
            to_bucket_context(opdata->p_detail->p_endpoint), std::string(opdata->p_detail->identifier), *ofs);

        headers = res->headers;

        return true;
    }

    bool head(const app::storage::basic_endpoint::pointer& ep, const identifier& id, headers_list& headers)
    {
        rml::s3::response_properties::pointer res = rml::s3::head(to_bucket_context(ep), std::string(id));
        if (res) {
            headers = res->headers;
            return true;
        }

        return false;
    }

protected:
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
