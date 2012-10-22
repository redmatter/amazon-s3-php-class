/*
 * whisper_client.hpp
 *
 *  Created on: 13 Jun 2011
 *      Author: dkorah
 */

#ifndef WHISPER_CLIENT_HPP_
#define WHISPER_CLIENT_HPP_

#include <rml/pc_channel.h>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

namespace app {

struct whisper
{
	const std::string cdr_path;
	const std::string fs_host;
	const std::string uuid;
	const time_t create_time;

	typedef boost::shared_ptr< whisper > pointer;

	friend std::ostream& operator<< (std::ostream& ostr, const whisper& w)
	{
		return ostr << w.cdr_path << ' ' << w.fs_host << ' ' << w.uuid << ' ' << w.create_time;
	}

	struct compare_pointer
	{
		bool operator()(const pointer& lhs, const pointer& rhs)
		{
			if(lhs->create_time != rhs->create_time)
				return lhs->create_time < rhs->create_time;

			return lhs->uuid < rhs->uuid;
		}
	};

	typedef rml::pc_channel<pointer, compare_pointer> channel_t;

	whisper(const std::string& cdr_path, const std::string& fs_host, const std::string& uuid, const time_t& create_time) :
		cdr_path(cdr_path), fs_host(fs_host), uuid(uuid), create_time(create_time)
	{
	}
};

class whisper_client
{
	enum constants
	{
		reconnect_delay_seconds = 5
	};

	struct state
	{
		enum state_t
		{
			initialised,

			connecting,

			ready_to_whisper,

			whisper_ack_wait,
			whisper_ack_ok,
			whisper_ack_err,

			nop_ack_wait,
			nop_ack_ok,
			nop_ack_err,

			reconnect_init,
			reconnecting,

			closing,
			closing_bye_wait,

			closed,

			n_states
		};

		static const char* to_string(const state_t& s)
		{
			static const char* str[n_states+1] = {
					"initialised",

					"connecting",

					"ready_to_whisper",

					"whisper_ack_wait",
					"whisper_ack_ok",
					"whisper_ack_err",

					"nop_ack_wait",
					"nop_ack_ok",
					"nop_ack_err",

					"reconnect_init",
					"reconnecting",

					"closing",
					"closing_bye_wait",

					"closed",

					"unknown"
			};

			return str[(s < 0 || s >= n_states)? n_states: s];
		}
	};

	boost::asio::io_service& io_service_;
	boost::asio::io_service::strand strand_;

	boost::asio::ip::tcp::resolver resolver_;
	boost::asio::ip::tcp::socket socket_;

	boost::asio::deadline_timer timer_;

	boost::asio::streambuf recv_buffer_;
	boost::asio::streambuf send_buffer_;

	const std::string distiller_host_;
	const std::string distiller_port_;

	bool connected_;
	state::state_t current_state_;

	whisper::channel_t& channel_;

public:
	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("distiller_client.server_port", boost::program_options::value<std::string>()->default_value("10001"), "Distiller port")
				("distiller_client.server_host", boost::program_options::value<std::string>()->default_value("cdrmunch.redmatter.com"), "Distiller host")
				("distiller_client.base_dir", boost::program_options::value<std::string>()->default_value("/mnt/rmfs/cdrmunch-cache"), "Path where whispered CDRs are stored")
				;
	}

	whisper_client(
			boost::asio::io_service& io_service,
			whisper::channel_t& channel,
			const std::string& distiller_host,
			const std::string& distiller_port) :
				io_service_(io_service),
				strand_(io_service_),
				resolver_(io_service_),
				socket_(io_service_),
				timer_(io_service_),
				distiller_host_(distiller_host),
				distiller_port_(distiller_port),
				connected_(false),
				current_state_(state::initialised),
				channel_(channel)
	{
		boost::asio::ip::tcp::resolver::query query(distiller_host_, distiller_port_);
	    resolver_.async_resolve(query,
	    		strand_.wrap(boost::bind(&whisper_client::handle_resolve, this,
	    				boost::asio::placeholders::error,
	    				boost::asio::placeholders::iterator)));

	    timer_.expires_from_now(boost::posix_time::seconds(10));
	    timer_.async_wait(
	    		strand_.wrap(boost::bind(&whisper_client::reconnect, this,
	    				false, boost::asio::placeholders::error)));
	}


	void close()
	{
		current_state_ = state::closing_bye_wait;

		std::ostream ostr(&send_buffer_); ostr.clear();
		ostr << "BYE\r\n";
		boost::asio::async_write(socket_, send_buffer_,
				strand_.wrap(boost::bind(&whisper_client::handle_write, this,
						boost::asio::placeholders::error,
						boost::asio::placeholders::bytes_transferred)));
	}

private:
	inline_ void reconnect(const bool& force_reconnect, const boost::system::error_code& error)
	{
		if(error != boost::asio::error::operation_aborted) {
			LogVerbose("Timeout; reconnecting...");
			reconnect(force_reconnect);
		}
	}

	inline_ void reconnect(const bool& force_reconnect = false)
	{
		if(connected_ && force_reconnect) {
			LogVerbose("Reconnecting...");
			do_close(true);
		}
		else if(!connected_ && (current_state_ == state::initialised)) {
			resolver_.cancel();
			if(socket_.is_open()) {
				boost::system::error_code ec;
				socket_.close(ec);
			}
			LogWarning("Distiller connection failed; retrying...");
		}

		if(!connected_) {
			LogMessage("Connecting to "<<distiller_host_<<':'<<distiller_port_);
			boost::asio::ip::tcp::resolver::query query(distiller_host_, distiller_port_);
		    resolver_.async_resolve(query,
		    		strand_.wrap(boost::bind(&whisper_client::handle_resolve, this,
		    				boost::asio::placeholders::error,
		    				boost::asio::placeholders::iterator)));

		    timer_.expires_from_now(boost::posix_time::seconds(10));
		    timer_.async_wait(
		    		strand_.wrap(boost::bind(&whisper_client::reconnect, this,
		    				false, boost::asio::placeholders::error)));
		}
	}

	void handle_resolve(const boost::system::error_code& error, boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
	{
		if (!error) {
			LogDebug("Resolved "<<distiller_host_<<':'<<distiller_port_<<"; opening connection");
			boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
			socket_.async_connect(endpoint,
					strand_.wrap(boost::bind(&whisper_client::handle_connect, this,
							boost::asio::placeholders::error, ++endpoint_iterator)));
		}
		else {
			LogWarning("Error resolving Distiller host "<<distiller_host_<<" ( port "<<distiller_port_<<" ) - error:"<<error.message());
		}
	}

	void handle_connect(const boost::system::error_code& error, boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
	{
		if (!error) {
			LogDebug("Connected to "<<distiller_host_<<':'<<distiller_port_<<"; handshaking...");
			current_state_ = state::connecting;
			timer_.cancel();

			run_statemachine(error);
		}
		else {
			LogWarning("Error connecting to Distiller host "<<distiller_host_<<" ( port "<<distiller_port_<<" ) - error:"<<error.message());
			if (endpoint_iterator != boost::asio::ip::tcp::resolver::iterator()) {
				boost::system::error_code ec;
				socket_.close(ec);

				boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
				socket_.async_connect(endpoint,
						strand_.wrap(boost::bind(&whisper_client::handle_connect, this,
								boost::asio::placeholders::error, ++endpoint_iterator)));
			}
		}
	}

	void run_statemachine(const boost::system::error_code& error)
	{
		// reset any timer in progress; if we loop through, we are alive and kicking
		timer_.cancel();

		if(error) {
			LogError("Error; current_state:"<<state::to_string(current_state_)<<": "<<error.message());
			current_state_ = (current_state_ < state::closing)? state::reconnect_init: state::closing;
		}

		switch(current_state_) {

		case state::reconnecting:
		case state::connecting:
		{
			connected_ = true;

			{
				boost::system::error_code ec;
				boost::asio::ip::tcp::socket::keep_alive option(true);
				socket_.set_option(option, ec);
				if(ec) {
					LogWarning("set_option failed; err:"<<ec.message());
					io_service_.post(
							strand_.wrap(boost::bind(&whisper_client::do_close, this, true))); // reconnect
					return;
				}
			}

			current_state_ = state::ready_to_whisper;
			run_statemachine(error);
			break;
		}

		case state::ready_to_whisper:
		case state::whisper_ack_ok:
		case state::nop_ack_ok:
		{
			std::ostream ostr(&send_buffer_); ostr.clear();
			try {
				whisper::pointer p_whisper = channel_.consume(10);
				current_state_ = state::whisper_ack_wait;
				ostr << *p_whisper << "\r\n";
			}
			catch(const whisper::channel_t::consume_timed_out_exception& e) {
				LogDebug("timed-out on whisper-item; retrying...");
				current_state_ = state::nop_ack_wait;

				ostr << "NOP\r\n";
			}
			catch(const whisper::channel_t::finishing_exception& e) {
				LogMessage(channel_<<e.what());
				current_state_ = state::closing;
				io_service_.post(strand_.wrap(boost::bind(&whisper_client::do_close, this)));

				return;
			}

			boost::asio::async_write(socket_, send_buffer_,
					strand_.wrap(boost::bind(&whisper_client::handle_write, this,
							boost::asio::placeholders::error,
							boost::asio::placeholders::bytes_transferred)));

			break;
		}

		case state::whisper_ack_wait:
		{
			std::string OK_or_ERR;
			char cr, lf;

			SetupRecvStream("Distiller", is, recv_buffer_);

			is >> OK_or_ERR >> cr >> lf;
			if(OK_or_ERR == "ERR") {
				LogError("Whisper acked ERR");
				current_state_ = state::whisper_ack_err;
			}
			else {
				LogVerbose("Whisper acked OK");
				current_state_ = state::whisper_ack_ok;
			}

			run_statemachine(error);

			break;
		}

		case state::nop_ack_wait:
		{
			std::string OK_or_ERR;
			char cr, lf;

			SetupRecvStream("Distiller", is, recv_buffer_);

			is >> OK_or_ERR >> cr >> lf;
			if(OK_or_ERR == "ERR") {
				LogError("NOP acked ERR");
				current_state_ = state::nop_ack_err;
			}
			else {
//				LogDebug("NOP acked OK");
				current_state_ = state::nop_ack_ok;
			}

			run_statemachine(error);

			break;
		}

		case state::nop_ack_err:
		case state::whisper_ack_err:
		{
			io_service_.post(strand_.wrap(boost::bind(&whisper_client::do_close, this, true)));
			break;
		}

		default:
		case state::closed:
			LogError("Invalid state; "<<current_state_);
			/* no break */

		case state::reconnect_init:
		{
			LogMessage("Reconnecting in "<<(int)reconnect_delay_seconds<<" seconds");
			io_service_.post(strand_.wrap(boost::bind(&whisper_client::do_close, this, true, reconnect_delay_seconds)));
			break;
		}

		case state::closing_bye_wait:
		case state::closing:
		{
			io_service_.post(boost::bind(&whisper_client::do_close, this));
			break;
		}

		}
	}

	void handle_write(const boost::system::error_code& error, std::size_t bytes_transferred)
	{
		if(!error) {
			if(current_state_ == state::nop_ack_wait || current_state_ == state::whisper_ack_wait) {

				boost::asio::async_read_until(socket_, recv_buffer_, "\r\n",
						strand_.wrap(boost::bind(&whisper_client::run_statemachine, this,
								boost::asio::placeholders::error)));

				timer_.expires_from_now(boost::posix_time::seconds(current_state_ == state::nop_ack_wait? 2: 5));
			    timer_.async_wait(strand_.wrap(boost::bind(&whisper_client::reconnect, this,
			    		true, boost::asio::placeholders::error)));

			    return;
			}
		}

		if(current_state_ == state::closing_bye_wait) {
			if(!error) {
				boost::asio::async_read_until(socket_, recv_buffer_, "\r\n",
						strand_.wrap(boost::bind(&whisper_client::run_statemachine, this,
								boost::asio::placeholders::error)));

				timer_.expires_from_now(boost::posix_time::seconds(2));
				timer_.async_wait(strand_.wrap(boost::bind(&whisper_client::force_close, this,
						boost::asio::placeholders::error)));
			}
			else
				io_service_.post(boost::bind(&whisper_client::force_close, this, error));

		    return;
		}

		if(!error)
			LogWarning("Invalid current_state:"<<current_state_);
		else
			LogError("Socket IO error: "<<error.message());

		current_state_ = state::reconnect_init;
		strand_.post(boost::bind(&whisper_client::run_statemachine, this, error));
	}

	void force_close(const boost::system::error_code& error)
	{
		if(!error)
			LogMessage("Force closing whisper connection; error: "<<error.message());
		do_close();
	}

	void do_close(const bool& reconnect, unsigned int reconnect_delay_seconds) {
		if (connected_) {
			current_state_ = state::closed;
			connected_ = false;

			boost::system::error_code ec;
			socket_.close(ec);
			resolver_.cancel();
			timer_.cancel(ec);
		}

		if(reconnect) {
			LogMessage("Reconnecting in "<<reconnect_delay_seconds<<" seconds");
			current_state_ = state::reconnecting;
			timer_.expires_from_now(boost::posix_time::seconds(reconnect_delay_seconds));
			timer_.async_wait(strand_.wrap(boost::bind(&whisper_client::reconnect, this,
					false, boost::asio::placeholders::error)));
		}
		else
			LogMessage("Closing... (will NOT reconnect)");
	}

	// instead of default params; to keep boost::bind errors at bay
	inline_ void do_close(const bool& reconnect) {
		do_close(reconnect, 5);
	}

	// instead of default params; to keep boost::bind errors at bay
	inline_ void do_close() {
		do_close(false);
	}
};

}  // namespace app

#endif /* WHISPER_CLIENT_HPP_ */
