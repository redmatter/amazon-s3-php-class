/*
 * put_server.hpp
 *
 *  Created on: 2 Feb 2011
 *      Author: dkorah
 */

#ifndef SERVER_HPP_
#define SERVER_HPP_

#include "put_session.hpp"
#include "operation_data.hpp"
#include "storage_endpoint.hpp"
#include "clear_cache.hpp"

#include "../common/types.hpp"
#include "../common/defines.h"
#include "../common/policy.hpp"

#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/shared_ptr.hpp>

namespace app {

class put_server
{
	boost::program_options::variables_map& confMap_;
	operation_data::channel_t& channel_;
	policy::cached_finder_t& cached_policy_finder_;
	storage::s3::endpoint::cached_finder_t& cached_endpoint_finder_;
	database::internal& internal_db_;
	clear_cache& clear_cache_;
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

	put_server(
			boost::program_options::variables_map& confMap,
			operation_data::channel_t& channel,
			policy::cached_finder_t& cached_policy_finder,
			storage::s3::endpoint::cached_finder_t& cached_endpoint_finder,
			database::internal& internal_db,
			clear_cache& clear_cache,
			boost::asio::io_service& io_service ) :
				confMap_(confMap),
				channel_(channel),
				cached_policy_finder_(cached_policy_finder),
				cached_endpoint_finder_(cached_endpoint_finder),
				internal_db_(internal_db),
				clear_cache_(clear_cache),
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

		put_session::pointer new_session(new put_session(confMap_, channel_, cached_policy_finder_, cached_endpoint_finder_, internal_db_, clear_cache_, io_service_));
		acceptor_.async_accept(new_session->socket(),
				boost::bind(&put_server::handle_accept, this, new_session,
						boost::asio::placeholders::error));
	}

	void handle_accept(put_session::pointer session, const boost::system::error_code& error)
	{
		if (!error) {
			session->start();
			put_session::pointer new_session(new put_session(confMap_, channel_, cached_policy_finder_, cached_endpoint_finder_, internal_db_, clear_cache_, io_service_));
			acceptor_.async_accept(new_session->socket(),
					boost::bind(&put_server::handle_accept, this, new_session,
							boost::asio::placeholders::error));
		}
	}
};

}  // namespace app

#endif /* SERVER_HPP_ */
