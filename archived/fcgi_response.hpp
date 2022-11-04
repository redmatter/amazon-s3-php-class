//
// Created by dkorah on 11/10/22.
//

#ifndef ARCHIVING_FCGI_RESPONSE_HPP
#define ARCHIVING_FCGI_RESPONSE_HPP

#include "../common/artefact/decoder_ostream.hpp"
#include "../common/coreapi_eventlog.hpp"

#include <rml/fcgi/iostreams_sink.hpp>
#include <rml/fcgi/request.hpp>
#include <rml/s3/common.hpp>

#include <boost/algorithm/string/replace.hpp>

#include <iostream>

namespace {
using app::types::data_sub_format;
using app::types::file_format;
} // namespace

namespace app {
namespace fcgi {
namespace response {

struct configurator
{
    configurator(const boost::shared_ptr<rml::fcgi::request>& r, const common::artefact::key_type& key,
                 file_format::file_format_t response_format, coreapi_eventlog::id_t audit_token,
                 const std::string& log_prefix)
        : request(r)
        , key(key)
        , response_format(response_format)
        , audit_token(audit_token)
        , log_prefix(log_prefix)
        , sink(r)
    {
    }

    static std::string get_mime_type(file_format::file_format_t target_format)
    {
        if (file_format::is_compressed(target_format) || file_format::is_encrypted(target_format)) {
            return "application/vnd.redmatter.archiving." +
                   boost::algorithm::replace_all_copy(std::string(file_format::to_string(target_format)), "_", "-");
        }

        return data_sub_format::to_mime_type(data_sub_format::data_sub_format_t(target_format & 0xff));
    }

    boost::shared_ptr<std::ostream> configure(const boost::shared_ptr<rml::s3::response_properties>& p)
    {
        // In practice, most files in storage are either encrypted & compressed, just encrypted (any imported MP3 call
        // recordings) or with neither encryption or compression. Just compression is possible, but unlikely.
        bool is_encrypted = p->headers.get("file-type-is-encrypted").get_value_or("false") == "true";
        bool is_compressed = p->headers.get("file-type-is-compressed").get_value_or("false") == "true";

        // Response format can either be plain (any compression or encryption undone) or be in the format they are
        // retained in storage. The latter case is when freeSWITCH requests buffered-recordings using a system-user's
        // authentication token.
        bool response_is_encrypted = file_format::is_encrypted(response_format);
        bool response_is_compressed = file_format::is_compressed(response_format);
        bool decryption_requested = !response_is_encrypted;
        bool decompression_requested = !response_is_compressed;

        // Based on the format we end-up sending in response, we have to decide what the content-length is going to be.
        // There is a slim chance that the metadata header for "clear-size" is missing in older files in storage. In
        // such cases no content-length header will be sent since sending a wrong content-length would do more harm than
        // good.
        char content_length_header[32] = {'\0'};
        {
            std::size_t content_length;
            bool suppress_content_length = false;
            if (response_is_compressed || response_is_encrypted) {
                // i.e. we are sending the file from storage without any processing, use the object's content-length
                content_length = p->content_length;
            } else {
                boost::optional<std::string> str = p->headers.get("clear-size");
                if (str == boost::none) {
                    LogWarning(log_prefix << "Could not figure out content length; suppressing Content-length header");
                    suppress_content_length = true;
                } else {
                    content_length = boost::lexical_cast<std::size_t>(*str);
                }
            }

            if (!suppress_content_length) {
                std::snprintf(content_length_header, 32, "Content-Length: %lu\r\n", content_length);
            }
        }

        std::string mime_type = get_mime_type(response_format);

        FCGX_FPrintF(request->fcgi_req.out,
                     "Status: 200 OK\r\n"
                     "Pragma: no-cache\r\n"
                     "Expires: 0\r\n"
                     "Cache-Control: no-store, no-cache, private, must-revalidate, post-check=0, pre-check=0\r\n"
                     "Content-Type: %s\r\n"
                     "%s"
                     "AuditLogToken: %d\r\n"
                     "X-File-Format: %d\r\n"
                     "\r\n",
                     mime_type.data(), content_length_header, audit_token, response_format);

        decoder_ostream = boost::make_shared<common::artefact::decoder_ostream>(
            sink, key, is_compressed && decompression_requested, is_encrypted && decryption_requested);

        return decoder_ostream;
    }

    const boost::shared_ptr<rml::fcgi::request> request;
    const common::artefact::key_type key;
    const file_format::file_format_t response_format;
    const coreapi_eventlog::id_t audit_token;
    const std::string log_prefix;

    rml::fcgi::iostreams_sink sink;
    boost::shared_ptr<common::artefact::decoder_ostream> decoder_ostream;
};

} // namespace response
} // namespace fcgi
} // namespace app

#endif // ARCHIVING_FCGI_RESPONSE_HPP
