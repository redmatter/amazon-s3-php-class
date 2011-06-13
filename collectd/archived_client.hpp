/*
 * archived_client.hpp
 *
 *  Created on: 29 Jan 2011
 *	  Author: dkorah
 */

#ifndef ARCHIVED_CLIENT_HPP_
#define ARCHIVED_CLIENT_HPP_

#include "../common/archivable.hpp"
#include "../common/defines.h"
#include "../common/types.hpp"

#include "globals.h"

#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>

namespace app {

// TODO timeouts

using boost::asio::ip::tcp;

class archived_client
{
private:
	//TODO optimize with masks for quicker switch case
	struct states
	{
		enum state
		{
			initialised,
			connecting,
			reconnecting,
//			connected,
			disconnected,

			handshake_recv,
			handshake_sent,
			handshake_err, // if CollectD received a wrong handshake initiation
			handshake_ack_waiting,
//			handshake_ack_ok,  // we go straight to ready_to_put from here
			handshake_ack_err, // if ArchiveD didnt like my version

			ready_to_put,

			put_sent,
			put_sent_ack_waiting,
			put_sent_ack_ok,
			put_sent_ack_err,
			put_sending_file,
//			put_sent_file,
			put_sent_file_ok,
			put_sent_file_err,
			put_sent_file_ack_waiting,
			put_sent_file_ack_ok,
			put_sent_file_ack_err,

			general_err,

			closing,
			closed
		};
	};

	boost::asio::io_service& io_service_;
	boost::asio::ip::tcp::resolver resolver_;
	tcp::socket socket_;

	boost::asio::deadline_timer timer_;

	boost::asio::streambuf recv_buffer_;
	boost::asio::streambuf send_buffer_;

	const std::string archived_host_;
	const std::string archived_port_;

	bool connected_;
	states::state current_state_;

	archiving::archivable_channel_t& channel_;
	archiving::archivable::pointer put_archivable_ptr_;
	size_t put_file_size_, put_sent_size_;

public:
	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("archived_client.server_port", boost::program_options::value<std::string>()->default_value(ARCHIVED_LISTEN_PORT), "ArchiveD listen port")
				("archived_client.server_host", boost::program_options::value<std::string>()->default_value("archived.redmatter.com"), "ArchiveD host name")
				;
	}

	archived_client(
			boost::asio::io_service& io_service,
			archiving::archivable_channel_t& channel,
			const std::string& archived_host,
			const std::string& archived_port) :
				io_service_(io_service),
				resolver_(io_service_),
				socket_(io_service_),
				timer_(io_service_),
				archived_host_(archived_host),
				archived_port_(archived_port),
				connected_(false),
				current_state_(states::initialised),
				channel_(channel)
	{
		boost::asio::ip::tcp::resolver::query query(archived_host_, archived_port_);
	    resolver_.async_resolve(query,
	    		boost::bind(&archived_client::handle_resolve, this,
	    				boost::asio::placeholders::error,
	    				boost::asio::placeholders::iterator));

	    timer_.expires_from_now(boost::posix_time::seconds(10));
	    timer_.async_wait(boost::bind(&archived_client::connect_archived, this,
	    		false, boost::asio::placeholders::error));
	}

	void close()
	{
		io_service_.post(boost::bind(&archived_client::do_close, this));
	}

private:
	inline void connect_archived(const bool& force_reconnect, const boost::system::error_code& error)
	{
		if(error != boost::asio::error::operation_aborted)
			connect_archived(force_reconnect);
	}

	inline void connect_archived(const bool& force_reconnect = false)
	{
		if(connected_ && force_reconnect) {
			LogMessage("Reconnecting...");
			do_close();
		}
		else if(!connected_ && (current_state_ == states::initialised)) {
			resolver_.cancel();
			socket_.cancel();
			socket_.close();
			connected_ = false;
			current_state_ = states::initialised;
			LogWarning("Archived connection failed; retrying...");
		}

		if(!connected_) {
			boost::asio::ip::tcp::resolver::query query(archived_host_, archived_port_);
		    resolver_.async_resolve(query,
		    		boost::bind(&archived_client::handle_resolve, this,
		    				boost::asio::placeholders::error,
		    				boost::asio::placeholders::iterator));

		    timer_.expires_from_now(boost::posix_time::seconds(10));
		    timer_.async_wait(boost::bind(&archived_client::connect_archived, this,
		    		false, boost::asio::placeholders::error));
		}
	}

	void handle_resolve(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
	{
		if (!err) {
			boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
			socket_.async_connect(endpoint,
					boost::bind(&archived_client::handle_connect, this,
							boost::asio::placeholders::error, ++endpoint_iterator));
		}
		else {
			LogWarning("Error resolving ArchiveD host "<<archived_host_<<" ( port "<<archived_port_<<" ) - error:"<<err.message());
		}
	}

	void handle_connect(const boost::system::error_code& error, tcp::resolver::iterator endpoint_iterator)
	{
		if (!error) {
			current_state_ = states::connecting;
			timer_.cancel();

			run_statemachine(error);
		}
		else {
			LogWarning("Error connecting to ArchiveD host "<<archived_host_<<" ( port "<<archived_port_<<" ) - error:"<<error.message());
			if (endpoint_iterator != tcp::resolver::iterator()) {
				socket_.close();

				boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
				socket_.async_connect(endpoint,
						boost::bind(&archived_client::handle_connect, this,
								boost::asio::placeholders::error, ++endpoint_iterator));
			}
		}
	}

	void run_statemachine(const boost::system::error_code& error)
	{
		// reset any timer in progress; if we loop through, we are alive and kicking
		timer_.cancel();

		if(error) {
			LogError("run_statemachine [current_state:"<<current_state_<<"]: "<<error.message());
			current_state_ = states::general_err;
		}

		switch(current_state_) {
		case states::connecting:
		{
			connected_ = true;

			{
				tcp::socket::keep_alive option(true);
				socket_.set_option(option);
			}

			current_state_ = states::handshake_recv;

			boost::asio::async_read_until(socket_, recv_buffer_, "\r\n",
					boost::bind(&archived_client::run_statemachine, this,
							boost::asio::placeholders::error));

			timer_.expires_from_now(boost::posix_time::seconds(2));
		    timer_.async_wait(boost::bind(&archived_client::connect_archived, this,
		    		true, boost::asio::placeholders::error));

			break;
		}

		case states::handshake_recv:
		{
			std::string ArchiveD, version_number, protocol, protocol_version_number;
			char v, sp[3], cr, lf;

			SetupRecvStream("ArchiveD", is, recv_buffer_);

			is >> ArchiveD >> sp[0] >> v >> version_number >> sp[1] >> protocol >> sp[2] >> protocol_version_number >> cr >> lf;

			std::ostream ostr(&send_buffer_); ostr.clear();

			if (ArchiveD != "ArchiveD" || protocol != "protocol") {
				ostr << "ERR invalid response\r\n";
				LogError("Error: Invalid response from server");
				current_state_ = states::handshake_err;
			}
			else if(protocol_version_number != archiving::put_protocol_version_str) {
				ostr << "ERR protocol version mismatch\r\n";
				LogError("Error: Server's protocol version ("<<protocol_version_number<<") not same as "<<archiving::put_protocol_version_str);
				current_state_ = states::handshake_err;
			}
			else {
				if(version_number != version_number_only_string)
					LogWarning("Warning: Server differs in version from client");

				ostr << "OK CollectD v"<<version_number_only_string<<" protocol "<<archiving::put_protocol_version_str<<"\r\n";

				current_state_ = states::handshake_sent;
			}

			boost::asio::async_write(socket_, send_buffer_,
					boost::bind(&archived_client::run_statemachine, this,
							boost::asio::placeholders::error));

			break;
		}


		case states::handshake_err:
		{
			current_state_ = states::closing;
			io_service_.post(boost::bind(&archived_client::do_close, this, true));
			break;
		}

		case states::handshake_sent:
		{
			current_state_ = states::handshake_ack_waiting;
			boost::asio::async_read_until(socket_, recv_buffer_, "\r\n",
					boost::bind(&archived_client::run_statemachine, this,
							boost::asio::placeholders::error));

			timer_.expires_from_now(boost::posix_time::seconds(2));
		    timer_.async_wait(boost::bind(&archived_client::connect_archived, this,
		    		true, boost::asio::placeholders::error));

			break;
		}

		case states::handshake_ack_waiting:
		{
			std::string OK_or_ERR, message;
			char sp, cr, lf;

			SetupRecvStream("ArchiveD", is, recv_buffer_);

			is >> OK_or_ERR;
			if(OK_or_ERR == "ERR") {
				is >> sp; std::getline(is, message, '\r'); is >> lf;
				LogError("handshake_ack_waiting: error: "<<message);
				current_state_ = states::handshake_ack_err;
				io_service_.post(boost::bind(&archived_client::do_close, this, true));
			}
			else {
				is >> cr >> lf;
				LogMessage("Handshake OK; Ready to PUT");
				current_state_ = states::ready_to_put;
				run_statemachine(error);
			}

			break;
		}

		case states::put_sent_file_ack_ok:
		{
			put_archivable_ptr_.reset();
		}
		case states::ready_to_put:
		{
			io_service_.post(boost::bind(&archived_client::get_archivable_for_put, this));
			break;
		}

		case states::put_sent:
		{
			current_state_ = states::put_sent_ack_waiting;
			boost::asio::async_read_until(socket_, recv_buffer_, "\r\n",
					boost::bind(&archived_client::run_statemachine, this,
							boost::asio::placeholders::error));

			timer_.expires_from_now(boost::posix_time::seconds(10));
		    timer_.async_wait(boost::bind(&archived_client::connect_archived, this,
		    		true, boost::asio::placeholders::error));

		    break;
		}

		case states::put_sent_ack_waiting:
		{
			std::string OK_or_ERR, PUT, md5, message;
			char sp[4], cr, lf;
			archiving::org_id OrgID;

			SetupRecvStream("ArchiveD", is, recv_buffer_);

			is >> OK_or_ERR >> sp[0] >> PUT >> sp[1] >> OrgID >> sp[2] >> md5;
			if(OK_or_ERR == "ERR") {
				is >> sp[3]; std::getline(is, message, '\r'); is >> lf;
				LogError("put_sent_ack_waiting: PUT Error: "<<message);
				current_state_ = states::put_sent_ack_err;
				io_service_.post(boost::bind(&archived_client::do_close, this, true));
			}
			else {
				is >> cr >> lf;
				LogMessage("PUT OK; Sending file");

				current_state_ = states::put_sending_file;
				rml::utils::tmp_fstream::pointer ifs = put_archivable_ptr_->get_fstream_ptr();
				ifs->re_open();
				ifs->rdbuf()->pubseekpos(0,std::ios::in);

				put_file_size_ = ifs->get_size();
				put_sent_size_ = 0;
				if(send_buffer_.size()) {
					LogWarning("send_buffer_.size() != 0 ["<<send_buffer_.size()<<"]; clearing it!");
					send_buffer_.consume(send_buffer_.size());
				}
				run_statemachine(error);
			}
			break;
		}

		case states::put_sending_file:
		{
			/*
			LogVerbose("-------------------------");
			LogVerbose("put_sent_size_: "<<put_sent_size_);
			LogVerbose("put_file_size_: "<<put_file_size_);
			if(put_sent_size_ < put_file_size_) {
				LogVerbose("send_buffer_.size(): "<<send_buffer_.size());
				std::streamsize buf_length = std::min<std::streamsize>( (std::streamsize)(put_file_size_ - put_sent_size_), (std::streamsize)4096 ) - send_buffer_.size();

				std::vector<char> buf(buf_length + 1);
				buf_length = put_archivable_ptr_->get_fstream_ptr()->rdbuf()->sgetn(&buf[0], buf_length);

				// TODO finetune memory usage here; now we double up unnecessarily!
				// we could read directly into allocated stream space [ after send_buffer_.prepare(buf_length) ]
				send_buffer_.sputn(&buf[0], buf_length);
				send_buffer_.commit(buf_length);

				put_sent_size_ += buf_length;

				if(put_sent_size_ == put_file_size_)
					send_buffer_.sputc('\x1a'); // add EOF

				boost::asio::async_write(socket_, send_buffer_,
						boost::bind(&archived_client::run_statemachine, this,
								boost::asio::placeholders::error));
			}
			else if(put_sent_size_ == put_file_size_) {
				if(send_buffer_.size()) {
					LogWarning("send_buffer_.size() != 0 ["<<send_buffer_.size()<<"]; retrying...");
					boost::asio::async_write(socket_, send_buffer_,
							boost::bind(&archived_client::run_statemachine, this,
									boost::asio::placeholders::error));
				}
				else {
					put_archivable_ptr_->get_fstream_ptr()->close();
					current_state_ = states::put_sent_file;
					run_statemachine(error);
				}
			}
			else {
				LogError("put_sent_size_ > put_file_size_ : invalid call sequence");
				current_state_ = states::general_err;
			}
			*/

			io_service_.post(boost::bind(&archived_client::put_send_file, this, error, 0));
			break;
		}

		case states::put_sent_file_err:
		{
			current_state_ = states::closing;
			io_service_.post(boost::bind(&archived_client::connect_archived, this,
					true, error));
			break;
		}

		case states::put_sent_file_ok:
		{
			current_state_ = states::put_sent_file_ack_waiting;
			boost::asio::async_read_until(socket_, recv_buffer_, "\r\n",
					boost::bind(&archived_client::run_statemachine, this,
							boost::asio::placeholders::error));

			timer_.expires_from_now(boost::posix_time::seconds(10));
		    timer_.async_wait(boost::bind(&archived_client::connect_archived, this,
		    		true, boost::asio::placeholders::error));

		    break;
		}

		case states::put_sent_file_ack_waiting:
		{
			std::string OK_or_ERR, RECV, md5, message;
			char sp[4], cr, lf;
			archiving::org_id OrgID;

			SetupRecvStream("ArchiveD", is, recv_buffer_);

			is >> OK_or_ERR >> sp[0] >> RECV >> sp[1] >> OrgID >> sp[2] >> md5;
			LogDebug("OK_or_ERR: "<<OK_or_ERR);
			if(OK_or_ERR != "OK") {
				is >> sp[3]; std::getline(is, message, '\r'); is >> lf;
				LogError("put_sent_file_ack_waiting: PUT Error: "<<message);
				current_state_ = states::put_sent_file_ack_err;
				io_service_.post(boost::bind(&archived_client::do_close, this, true));
			}
			else {
				is >> cr >> lf;
				LogMessage("PUT OK; Get next for PUT");
				current_state_ = states::put_sent_file_ack_ok;
				run_statemachine(error);
			}

			break;
		}

		default:
			LogError("State NOT implemented OR Invalid!");

		case states::general_err:
		{
			current_state_ = states::closing;
			io_service_.post(boost::bind(&archived_client::connect_archived, this,
					true, error));
			break;
		}

		} // switch
	}

	void get_archivable_for_put()
	{
		try {
			if(!(current_state_ == states::ready_to_put || current_state_ == states::put_sent_file_ack_ok) || put_archivable_ptr_.get()) {

				if(!(current_state_ == states::ready_to_put || current_state_ == states::put_sent_file_ack_ok))
					LogError("Invalid call sequence; current_state_ == states::ready_to_put || current_state_ == states::put_sent_file_ack_ok");

				if(put_archivable_ptr_)
					LogError("Invalid call sequence; put_archivable_ptr_ != NULL");

				current_state_ = states::closing;
				io_service_.post(boost::bind(&archived_client::do_close, this, true));
				return;
			}

			put_archivable_ptr_ = channel_.consume(10/*seconds*/);
			LogMessage("Got from "<<channel_<<" archivable: "<<*(put_archivable_ptr_.get()));

			std::ostream ostr(&send_buffer_); ostr.clear();

			// ostr << "PUT " << put_archivable_ptr_->get_org_id() << ' ' << put_archivable_ptr_->get_md5() << ' ' << put_archivable_ptr_->get_fstream_ptr()->get_size() << "\r\n";

			put_archivable_ptr_->shove_put_request(ostr);

			current_state_ = states::put_sent;
			boost::asio::async_write(socket_, send_buffer_,
					boost::bind(&archived_client::run_statemachine, this,
							boost::asio::placeholders::error));
		}
		catch(const archiving::archivable_channel_t::consume_timed_out_exception& e) {
			LogVerbose("timed-out; retrying");
			io_service_.post(boost::bind(&archived_client::get_archivable_for_put, this));
		}
		catch(const archiving::archivable_channel_t::finishing_exception& e) {
			LogMessage("channel finishing: closing");
			io_service_.post(boost::bind(&archived_client::do_close, this));
			current_state_ = states::closing;
		}
	}

	void put_send_file(const boost::system::error_code& error, std::size_t bytes_transferred)
	{
		LogVerbose("-------------------------");
		LogVerbose("put_sent_size_: "<<put_sent_size_);
		LogVerbose("bytes_transferred: "<<bytes_transferred);
		LogVerbose("error: "<<error);

		if(error || ( !error && (put_sent_size_ == put_file_size_) )) {
			if (!error && (put_sent_size_ == put_file_size_)) {
				{
					std::string eof("\x1a");
					boost::asio::write(socket_, boost::asio::buffer(eof, 1));
				}
				LogMessage("PUT send file completed OK");
				current_state_ = states::put_sent_file_ok;
			}
			else {
				LogError("PUT send file completed with error; err:"<<error.message());
				current_state_ = states::put_sent_file_err;
			}
			io_service_.post(boost::bind(&archived_client::run_statemachine, this, error));
			return;
		}

		std::streamsize buf_length = std::min<std::streamsize>( (std::streamsize)(put_file_size_ - put_sent_size_), (std::streamsize)4096 );
		LogDebug("1. buf_length: "<<buf_length);

		// TODO finetune memory usage here; now we double up unnecessarily!
		// we could read directly into allocated stream space [ after send_buffer_.prepare(buf_length) ]

		std::vector<char> buf(buf_length + 1);
		buf_length = put_archivable_ptr_->get_fstream_ptr()->rdbuf()->sgetn(&buf[0], buf_length);

		LogDebug("2. buf_length: "<<buf_length);
		put_sent_size_ += buf_length;

		/*
		if(put_sent_size_ == put_file_size_) {
			buf[++buf_length] = '\t'; // add EOF
			LogDebug("3. buf_length: "<<buf_length);
		}
		*/

		std::ostream ostr(&send_buffer_); ostr.clear();
		ostr.write(&buf[0], buf_length);

		boost::asio::async_write(socket_, send_buffer_,
				boost::bind(&archived_client::put_send_file, this,
						boost::asio::placeholders::error,
						boost::asio::placeholders::bytes_transferred));
	}

	void do_close(const bool& reconnect, unsigned int reconnect_delay_seconds) {
		current_state_ = states::closed;
		connected_ = false;

		socket_.cancel();
		socket_.close();
		resolver_.cancel();
		timer_.cancel();

		if(reconnect) {
		    timer_.expires_from_now(boost::posix_time::seconds(reconnect_delay_seconds));
		    timer_.async_wait(boost::bind(&archived_client::connect_archived, this,
		    		false, boost::asio::placeholders::error));
		}
	}

	// instead of default params; to keep boost::bind errors at bay
	inline void do_close(const bool& reconnect) {
		do_close(reconnect, 5);
	}

	// instead of default params; to keep boost::bind errors at bay
	inline void do_close() {
		do_close(false);
	}
};

}  // namespace app

#endif /* ARCHIVED_CLIENT_HPP_ */
