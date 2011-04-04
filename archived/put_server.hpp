/*
 * put_server.hpp
 *
 *  Created on: 2 Feb 2011
 *      Author: dkorah
 */

#ifndef SERVER_HPP_
#define SERVER_HPP_

#include "put_session.hpp"

#include "../common/types.hpp"
#include "../common/defines.h"

#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/shared_ptr.hpp>

namespace app {

class put_server
{
	boost::program_options::variables_map& confMap_;
	archiving::archivable_channel_t& channel_;
	boost::asio::io_service& io_service_;
	boost::asio::ip::tcp::acceptor acceptor_;

public:
	typedef boost::shared_ptr<put_server> pointer;

	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("archived.put_port",  boost::program_options::value<unsigned short>()->default_value(archiving::ports::put), "ArchiveD PUT port")
				;
	}

	put_server(boost::program_options::variables_map& confMap, archiving::archivable_channel_t& channel, boost::asio::io_service& io_service)
		: confMap_(confMap),
		  channel_(channel),
		  io_service_(io_service),
		  acceptor_(io_service)
	{
		const unsigned short& put_port = confMap_["archived.put_port"].as<unsigned short>();

		// we could add an address to listen on; bit OTT for our use!
		boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), put_port);
		boost::system::error_code ec;
		acceptor_.open(endpoint.protocol());
		acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
		acceptor_.bind(endpoint);
		acceptor_.listen(boost::asio::socket_base::max_connections, ec);
		if(ec)
			THROW_RML_EXCEPTION("Failed to listen on PUT port ["<<put_port<<"]; error:"<<ec.message());

		LogMessage("Listening on PUT port ["<<put_port<<']');

		put_session::pointer new_session(new put_session(confMap_, channel_, io_service_));
		acceptor_.async_accept(new_session->socket(),
				boost::bind(&put_server::handle_accept, this, new_session,
						boost::asio::placeholders::error));
	}

	void handle_accept(put_session::pointer session, const boost::system::error_code& error)
	{
		if (!error) {
			session->start();
			put_session::pointer new_session(new put_session(confMap_, channel_, io_service_));
			acceptor_.async_accept(new_session->socket(),
					boost::bind(&put_server::handle_accept, this, new_session,
							boost::asio::placeholders::error));
		}
	}
};

}  // namespace app

#endif /* SERVER_HPP_ */
