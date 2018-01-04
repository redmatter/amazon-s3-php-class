//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 13/12/2017.
//

#ifndef ARCHIVING_ESTUARY_APP_ERRORS_HPP
#define ARCHIVING_ESTUARY_APP_ERRORS_HPP

#include <boost/type_traits/integral_constant.hpp>

namespace app
{
namespace error
{
namespace http
{

enum enum_t
{
    continue_ = 100,

    ok = 200,
    created = 201,
    accepted = 202,
    no_content = 204,

    multiple_choices = 300,
    moved_permanently = 301,
    moved_temporarily = 302,
    not_modified = 304,

    bad_request = 400,
    unauthorized = 401,
    forbidden = 403,
    not_found = 404,
    method_not_allowed = 405,
    not_acceptable = 406,
    length_required = 411,

    internal_server_error = 500,
    not_implemented = 501,
    bad_gateway = 502,
    service_unavailable = 503,
    insufficient_storage = 507
};

inline
bool has_response_body(const enum_t e)
{
    switch (e) {
    case accepted:
    case no_content:
    case not_modified:
        return false;
    default:
        return true;
    }
}

} // namespace http
} // namespace error
} // namespace app

namespace boost
{
namespace system
{
class error_condition;

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

inline error_condition make_error_condition(app::error::http::enum_t e) BOOST_NOEXCEPT;

#pragma clang diagnostic pop
} // namespace system
} // namespace boost

// Include here so that the compiler sees make_error_condition override; that way we dont need special
// functions to make error_condition instances.
#include <boost/system/error_code.hpp>
#include <boost/exception/error_info.hpp>

namespace app
{
namespace error
{

struct http_category : public boost::system::error_category
{
    const char* name() const
    { return "http"; }

    std::string message(int ev) const
    {
        switch (static_cast<http::enum_t>(ev)) {

        case http::continue_:
            return "Continue";

        case http::ok:
            return "OK";
        case http::created:
            return "Created";
        case http::accepted:
            return "Accepted";
        case http::no_content:
            return "No Content";

        case http::multiple_choices:
            return "Multiple Choices";
        case http::moved_permanently:
            return "Moved Permanently";
        case http::moved_temporarily:
            return "Moved Temporarily";
        case http::not_modified:
            return "Not Modified";

        case http::bad_request:
            return "Bad Request";
        case http::unauthorized:
            return "Unauthorized";
        case http::forbidden:
            return "Forbidden";
        case http::not_found:
            return "Not Found";
        case http::method_not_allowed:
            return "Method Not Allowed";
        case http::not_acceptable:
            return "Not Acceptable";
        case http::length_required:
            return "Length Required";

        case http::internal_server_error:
            return "Internal Server Error";
        case http::not_implemented:
            return "Not Implemented";
        case http::bad_gateway:
            return "Bad Gateway";
        case http::service_unavailable:
            return "Service Unavailable";
        case http::insufficient_storage:
            return "Insufficient Storage";
        }
    }
};

http_category& get_http_category()
{
    static http_category category;
    return category;
}

struct tag_error_condition;
typedef boost::error_info<tag_error_condition, boost::system::error_condition> error_info_error_condition;

struct tag_internal_error_message;
typedef boost::error_info<tag_internal_error_message, std::string> error_info_internal_error_message;

struct http_error_exception : public std::runtime_error
{
    explicit http_error_exception(const std::string& message) : runtime_error(message)
    {}

    http_error_exception() : runtime_error("")
    {}
};

struct internal_error_exception : public std::runtime_error
{
    internal_error_exception() : runtime_error("Internal Error")
    {}
};

} // namespace error
} // namespace app

namespace boost
{
namespace system
{

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedStructInspection"
template<>
struct is_error_condition_enum<app::error::http::enum_t>
{
    static const bool value = true;
};
#pragma clang diagnostic pop

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

inline error_condition make_error_condition(app::error::http::enum_t e) BOOST_NOEXCEPT
{
    return error_condition(static_cast<int>(e), app::error::get_http_category());
}

#pragma clang diagnostic pop

} // namespace system
} // namespace boost

#endif //ARCHIVING_ESTUARY_APP_ERRORS_HPP
