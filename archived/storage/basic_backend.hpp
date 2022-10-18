//
// Created by dkorah on 05/10/22.
//

#ifndef ARCHIVING_STORAGE_BASIC_BACKEND_HPP
#define ARCHIVING_STORAGE_BASIC_BACKEND_HPP

#include "../operation_data.hpp"

#include <rml/s3/delete_multiple.hpp>
#include <rml/s3/headers_list.hpp>
#include <rml/s3/get_stream.hpp>

namespace {
using app::operation_data;
using app::storage::basic_endpoint;
using archiving::archivable;
using boost::program_options::variables_map;
} // namespace

namespace archiving {
namespace archived {
namespace storage {

typedef rml::s3::headers_list headers_list;
typedef rml::s3::delete_multiple_request delete_keys_request;
typedef rml::s3::delete_multiple_result delete_keys_result;
typedef rml::s3::response_properties_callback headers_callback;
typedef archivable::identifier identifier;

struct basic_backend
{
    virtual bool put(const operation_data::pointer& opdata) = 0;
    virtual bool get(const operation_data::pointer& opdata, headers_list& headers) = 0;
    virtual bool get(const boost::shared_ptr<basic_endpoint>& ep, const identifier& id,
                     const headers_callback& callback) = 0;
    virtual bool head(const boost::shared_ptr<basic_endpoint>& ep, const identifier& id, headers_list& headers,
                      uint64_t& content_length, bool& not_found) = 0;
    virtual delete_keys_result delete_multiple(const boost::shared_ptr<basic_endpoint>& ep,
                                               const delete_keys_request& keys_list, delete_keys_result& result) = 0;

    bool head(const operation_data::pointer& opdata, headers_list& headers, uint64_t& content_length, bool& not_found)
    {
        return head(opdata->p_detail->p_endpoint, opdata->p_detail->identifier, headers, content_length, not_found);
    }
};

template<typename Iterator>
bool
delete_multiple(const boost::shared_ptr<basic_backend>& b,
                const boost::shared_ptr<basic_endpoint>& ep,
                const Iterator& begin,
                const Iterator& end,
                const boost::function<std::string(const Iterator&)>& get_key,
                delete_keys_result& result)
{
    if (begin == end) {
        return true;
    }

    delete_keys_request r;
    for (Iterator i = begin; i != end; ++i) {
        r.add_key(get_key(i));
    }

    return b->delete_multiple(ep, r, result);
}

} // namespace storage
} // namespace archived
} // namespace archiving

#endif // ARCHIVING_STORAGE_BASIC_BACKEND_HPP
