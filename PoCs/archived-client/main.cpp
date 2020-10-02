//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 27/09/2020.
//

#include "../blow_bubbles.hpp"

#include "../../common/archivable.hpp"
#include "../../common/net/archived/connection.hpp"
#include "../../common/net/archived/send_file.hpp"

#include <boost/asio.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>
#include <boost/random.hpp>
#include <boost/thread.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <cstdlib>
#include <fstream>


#define MAX_FILE_SIZE 5 * 1024 * 1024
#define MIN_FILE_SIZE 5 * 1024

#define BUF_SIZE (16 * 1024)

int
get_seed()
{
    std::ifstream rand("/dev/urandom", std::ios::in | std::ios::binary);
    int seed;
    rand.read(reinterpret_cast<char*>(&seed), sizeof(int));
    rand.close();

    return seed;
}

std::size_t
get_size()
{
    static boost::random::mt19937 rng(get_seed());
    static boost::uniform_int<std::size_t> size_dist(MIN_FILE_SIZE, MAX_FILE_SIZE);
    static boost::variate_generator<boost::random::mt19937, boost::uniform_int<std::size_t> > rand_size(rng, size_dist);

    return rand_size();
}

std::pair<std::size_t, std::string>
get_file()
{
    boost::filesystem::path temp = boost::filesystem::unique_path();

    std::ifstream rand("/dev/urandom", std::ios::in | std::ios::binary);
    if (!rand) {
        throw std::runtime_error("Failed to open urandom");
    }

    std::ofstream ofs(temp.c_str(), std::ios::out | std::ios::binary);

    std::size_t file_size = get_size();
    char buf[BUF_SIZE];
    for (std::size_t chunks = (std::size_t)std::ceil((float)file_size / (float)BUF_SIZE); chunks > 0; chunks--) {
        if (!rand.read(buf, (chunks > 1) ? BUF_SIZE : file_size % BUF_SIZE)) {
            throw std::runtime_error("Cannot read from urandom");
        }

        if (!ofs.write(buf, rand.gcount())) {
            throw std::runtime_error("Cannot write to " + temp.string());
        }
    }

    return std::make_pair(file_size, temp.string());
}

unsigned
get_count()
{
    static unsigned count = 1;
    if (count == 1) {
        const char* count_env = std::getenv("COUNT");
        if (count_env) {
            count = boost::lexical_cast<unsigned>(count_env);
        }
    }

    return count;
}

typedef common::net::archived::connection connection;
typedef boost::asio::ip::address ip_address;

boost::shared_ptr<connection>
get_connection(boost::asio::io_service& io_service)
{
    static ip_address archived_ip;
    static unsigned short archived_port = 0;

    boost::system::error_code ec;

    if (archived_ip.is_unspecified()) {
        const char* ip_env = std::getenv("ARCHIVED_HOST");
        if (!ip_env) {
            throw std::runtime_error("ARCHIVED_HOST not found in env");
        }

        archived_ip = ip_address::from_string(ip_env, ec);
        if (ec) {
            throw std::runtime_error("Cannot parse IP from " + std::string(ip_env));
        }
    }

    if (!archived_port) {
        const char* port_env = std::getenv("ARCHIVED_PORT");
        if (!port_env) {
            throw std::runtime_error("ARCHIVED_PORT not found in env");
        }

        archived_port = boost::lexical_cast<unsigned short>(port_env);
    }

    boost::shared_ptr<connection> c =
        boost::make_shared<connection>(boost::ref(io_service), boost::cref(archived_ip), archived_port);

    c->start(ec);
    if (ec) {
        throw std::runtime_error("Connection failed: err:" + ec.message());
    }

    return c;
}

void
send_file_done_callback(bool& done,
                        const boost::system::error_code& ec,
                        const boost::optional<boost::chrono::steady_clock::time_point>& retry_after)
{
    done = true;
    if (ec) {
        std::cerr << "Failed with error " << ec.message() << std::endl;
    }
}

bool
is_interrupted()
{
    return false;
}

int
main(int argc, char* argv[])
{
    try {
        boost::asio::io_service io_service;
        boost::shared_ptr<boost::asio::io_service::work> p_work =
            boost::make_shared<boost::asio::io_service::work>(boost::ref(io_service));

        boost::thread_group threads;

        const unsigned n_service_threads = 2;
        for (unsigned int i = 0; i < n_service_threads; i++) {
            threads.create_thread(boost::bind(&boost::asio::io_service::run, boost::ref(io_service)));
        }

        const boost::shared_ptr<connection>& conn = get_connection(io_service);

        for (unsigned i = get_count(); i > 0; i--) {
            std::string uuid = boost::lexical_cast<std::string>(boost::uuids::random_generator()());

            const std::pair<std::size_t, std::string>& file_info = get_file();

            boost::shared_ptr<archiving::archivable> a =
                boost::shared_ptr<archiving::archivable>(new archiving::archivable(
                    uuid, uuid, 1601301733, file_info.second, archiving::file_type::recording, "wav", "test"));

            archiving::owner_info o;
            o.orgID = 1;
            a->set_owners(o);

            bool done = false;
            common::net::archived::send_file(
                conn, a, boost::bind(send_file_done_callback, boost::ref(done), _1, _2), is_interrupted);

            poc::blow_bubbles_till(done);

            boost::filesystem::remove(file_info.second);
        }

        exit(0);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        exit(1);
    }
}
