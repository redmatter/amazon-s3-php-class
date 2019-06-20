//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 19/06/2019.
//

#ifndef ARCHIVING_COMMON_NET_ARCHIVED_PUT_FILE_HPP
#define ARCHIVING_COMMON_NET_ARCHIVED_PUT_FILE_HPP

#include "../../archivable.hpp"
#include "put_state.hpp"

#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/bind/protect.hpp>

namespace common
{
namespace net
{
namespace archived
{
namespace detail
{

struct put_file_session;

std::ostream& operator<<(std::ostream&, const put_file_session&);

struct put_file_session
{
    const boost::shared_ptr<archiving::archivable> file;
    const boost::function<void(boost::system::error_code& ec)> done_callback;
    boost::shared_ptr<rml::utils::tmp_fstream> file_stream;
    std::size_t file_size_remaining;
    detail::put_state::enum_t state;

    put_file_session(const boost::shared_ptr<archiving::archivable>& file,
                     const boost::function<void(boost::system::error_code& ec)> done_callback) :
            file(file), done_callback(done_callback),
            file_size_remaining(0), state(detail::put_state::init)
    {}

    void post_error(boost::asio::io_service& ios, const boost::system::error_code& ec)
    {
        ios.post(boost::bind(boost::protect(done_callback), ec));
    }

    void post_error(boost::asio::io_service& ios, const error::enum_t& e)
    {
        post_error(ios, error::make_error_code(e));
    }

    void post_success(boost::asio::io_service& ios)
    {
        post_error(ios, boost::system::error_code());
    }

    bool prepare() try
    {
        file_stream = file->get_fstream_ptr();
        file_stream->re_open();

        file_size_remaining = file_stream->get_size();

        // make sure the get pointer is at the beginning of the file stream
        file_stream->rdbuf()->pubseekpos(0, std::ios::in);

        return true;
    } catch (const std::exception& e) {
        LogWarning(*this << " error:" << e.what());
        return false;
    }
};

std::ostream& operator<<(std::ostream& os, const put_file_session& s)
{
    return s.file->identify_without_state(os) << ' ' << s.state;
}

} // namespace detail
} // namespace archived
} // namespace net
} // namespace common

#endif //ARCHIVING_COMMON_NET_ARCHIVED_PUT_FILE_HPP
