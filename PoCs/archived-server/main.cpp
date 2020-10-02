//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 29/09/2020.
//

#include "../../archived/net/put/session.hpp"
#include "../blow_bubbles.hpp"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/make_shared.hpp>
#include <boost/thread.hpp>

#include <boost/lexical_cast.hpp>
#include <cstdlib>
#include <exception>
#include <iostream>

unsigned short
get_port()
{
    const char* port_env = std::getenv("ARCHIVED_PORT");
    if (!port_env) {
        throw std::runtime_error("ARCHIVED_PORT not found in env");
    }

    return boost::lexical_cast<unsigned short>(port_env);
}

std::size_t
get_receive_buffer_size()
{
    static boost::optional<std::size_t> value = boost::none;

    if (value == boost::none) {
        const char* str = std::getenv("RECV_BUFFER_SIZE");
        if (str) {
            value = boost::lexical_cast<std::size_t>(str);
        } else {
            value = 0;
        }
    }

    return *value;
}

bool
get_no_delay()
{
    static boost::optional<std::size_t> value = boost::none;

    if (value == boost::none) {
        const char* str = std::getenv("NO_DELAY");
        if (str) {
            value = 1 == boost::lexical_cast<int>(str);
        } else {
            value = false;
        }
    }

    return *value;
}

boost::optional<std::time_t>
should_neglect_pcaps()
{
    return boost::none;
}

bool validate_policy(archiving::org_id_t, app::policy::id_type)
{
    return true;
}

void
request_handler(const boost::shared_ptr<archiving::archived::net::put::request>& req)
{
    std::cerr << req->storage_key << " handled" << std::endl;
}

void
handle_accept(boost::asio::io_service& io_service,
              boost::asio::ip::tcp::acceptor& acceptor,
              const boost::shared_ptr<archiving::archived::net::put::session>& session,
              const boost::system::error_code& ec);

void
accept_next(boost::asio::io_service& io_service, boost::asio::ip::tcp::acceptor& acceptor)
{
    using archiving::archived::net::put::session;

    boost::shared_ptr<session> new_session = boost::make_shared<session>(
        boost::ref(io_service), "/tmp", should_neglect_pcaps, validate_policy, request_handler);

    acceptor.async_accept(new_session->get_socket(),
                          boost::bind(handle_accept,
                                      boost::ref(io_service),
                                      boost::ref(acceptor),
                                      new_session,
                                      boost::asio::placeholders::error));
}

void
handle_accept(boost::asio::io_service& io_service,
              boost::asio::ip::tcp::acceptor& acceptor,
              const boost::shared_ptr<archiving::archived::net::put::session>& session,
              const boost::system::error_code& ec)
{
    if (ec) {
        std::cerr << "handle_accept error:" << ec.message() << std::endl;
    }

    session->start(get_no_delay(), get_receive_buffer_size());
    accept_next(io_service, acceptor);
}

int
main(int argc, char* argv[])
try {
    using archiving::archived::net::put::session;

    boost::asio::io_service io_service;
    boost::shared_ptr<boost::asio::io_service::work> p_work =
        boost::make_shared<boost::asio::io_service::work>(boost::ref(io_service));

    boost::thread_group threads;

    const unsigned n_service_threads = 2;
    for (unsigned int i = 0; i < n_service_threads; i++) {
        threads.create_thread(boost::bind(&boost::asio::io_service::run, boost::ref(io_service)));
    }

    boost::asio::ip::tcp::acceptor acceptor(io_service);

    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), get_port());
    boost::system::error_code ec;
    acceptor.open(endpoint.protocol(), ec);
    if (ec) {
        throw std::runtime_error("Failed to initialise (acceptor); err:" + ec.message());
    }

    acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
    if (ec) {
        throw std::runtime_error("Failed to initialise (acceptor : set_option reuse_address); err:" + ec.message());
    }

    acceptor.bind(endpoint);
    acceptor.listen(boost::asio::socket_base::max_connections, ec);
    if (ec) {
        throw std::runtime_error("Failed to initialise  (acceptor : listen); err:" + ec.message());
    }

    accept_next(io_service, acceptor);

    poc::blow_bubbles();
} catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    exit(1);
}
