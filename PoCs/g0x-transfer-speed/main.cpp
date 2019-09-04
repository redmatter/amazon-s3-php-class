//
// Copyright (c) 2019 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 29/08/2019.
//

#include <iostream>
#include <iomanip>

#include <boost/asio.hpp>
#include <boost/cstdint.hpp>
#include <boost/chrono/system_clocks.hpp>
#include <boost/chrono/io/duration_io.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <boost/bind.hpp>
#include <boost/system/error_code.hpp>

#define Log(line) \
    std::cerr << __FILE__ << ':' << __LINE__ << ' ' << line << std::endl

using boost::asio::ip::tcp;

class client
{
    typedef boost::array<char, 16 * 1024> buf_type;
public:
    client(boost::asio::io_service& io_service, const std::string& server, unsigned port, std::size_t to_send)
            : resolver_(io_service), socket_(io_service), buffer(), to_send(to_send), wrote_bytes(0)
    {
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd == -1) {
            throw std::runtime_error("failed to open /dev/urandom");
        }
        read(fd, buffer.c_array(), buf_type::size());
        close(fd);

        tcp::resolver::query query(server, boost::lexical_cast<std::string>(port));
        resolver_.async_resolve(query,
                                boost::bind(&client::handle_resolve, this,
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::iterator));
    }

private:
    void handle_resolve(const boost::system::error_code& err, tcp::resolver::iterator endpoint_iterator)
    {
        if (!err) {
            tcp::endpoint endpoint = *endpoint_iterator;
            socket_.async_connect(endpoint,
                                  boost::bind(&client::handle_connect, this,
                                              boost::asio::placeholders::error, ++endpoint_iterator));
        } else {
            Log("Error: " << err);
        }
    }

    void handle_connect(const boost::system::error_code& err,
                        tcp::resolver::iterator endpoint_iterator)
    {
        if (!err) {
            boost::system::error_code ec;
//            boost::asio::socket_base::send_buffer_size send_buffer_size_option(32 * 1024);
//            socket_.set_option(send_buffer_size_option, ec);

            {
                boost::asio::socket_base::send_buffer_size opt;
                socket_.get_option(opt);
                Log("send_buffer_size_option: " << opt.value());
            }

            tcp::socket::keep_alive keep_alive_option(true);
            socket_.set_option(keep_alive_option, ec);
            if (ec) {
                return;
            }

            start_time = boost::chrono::steady_clock::now();
            write_data(err);
        } else if (endpoint_iterator != tcp::resolver::iterator()) {
            // The connection failed. Try the next endpoint in the list.
            socket_.close();
            tcp::endpoint endpoint = *endpoint_iterator;
            socket_.async_connect(endpoint,
                                  boost::bind(&client::handle_connect, this,
                                              boost::asio::placeholders::error, ++endpoint_iterator));
        } else {
            Log("Error: " << err);
        }
    }

    void write_data(const boost::system::error_code& err)
    {
        if (!err) {
            if (to_send == 0) {
                Log("Finished sending data");
                print_stats();
                return;
            }

            std::size_t n = std::min(to_send, buf_type::size());

            std::ostream ostr(&send_buffer_);
            ostr.write(buffer.c_array(), n);

            boost::asio::async_write(socket_, send_buffer_,
                                     boost::bind(&client::write_data, this,
                                                 boost::asio::placeholders::error));
            wrote_bytes += n;
            to_send -= n;
        } else {
            Log("Error: " << err);
        }
    }

    void print_stats()
    {
        boost::chrono::milliseconds took = boost::chrono::duration_cast<boost::chrono::milliseconds>(
                boost::chrono::steady_clock::now() - start_time);
        Log("Sent " << wrote_bytes << " bytes in " << took);
        Log("Transfer rate: " << std::fixed << std::setprecision(2)
                              << (static_cast<double >(wrote_bytes) / static_cast<double>(took.count())) << " kB/s");
    }

    tcp::resolver resolver_;
    tcp::socket socket_;
    boost::asio::streambuf send_buffer_;
    buf_type buffer;
    boost::chrono::steady_clock::time_point start_time;
    std::size_t to_send;
    std::size_t wrote_bytes;
};

int main(int argc, char* argv[])
{
    try {
        boost::program_options::variables_map conf;
        boost::program_options::options_description opts_desc(
                "send-speed test utility"
                "\nOptions", 100, 50);
        opts_desc.add_options()
                ("help", "this help")
                ("host", boost::program_options::value<std::string>()->default_value("archived.redmatter.com"),
                 "server host name")
                ("port", boost::program_options::value<unsigned>()->default_value(5555), "server host port")
                ("n", boost::program_options::value<std::size_t>()->default_value(10 * 1024 * 1024), "number of bytes to send");

        boost::program_options::store(boost::program_options::parse_command_line(argc, argv, opts_desc), conf);
        boost::program_options::notify(conf);

        if (conf.count("help")) {
            std::cout << opts_desc << "\n";
            exit(0);
        }

        boost::asio::io_service io_service;
//        boost::shared_ptr<boost::asio::io_service::work> p_work =
//                boost::make_shared<boost::asio::io_service::work>(boost::ref(io_service));
//        boost::thread_group threads;
        client c(io_service, conf["host"].as<std::string>(), conf["port"].as<unsigned>(), conf["n"].as<std::size_t>());
        io_service.run();
    }
    catch (const std::exception& e) {
        Log("Exception: " << e.what());
        exit(1);
    }
    catch (...) {
        Log("Unexpected exception");
        exit(1);
    }

    exit(0);
}
