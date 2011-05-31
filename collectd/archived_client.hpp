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
			put_sent_file,
			put_sent_file_ack_waiting,
			put_sent_file_ack_ok,
			put_sent_file_ack_err,

			general_err,

			closing,
			closed
		};
	};

	boost::asio::io_service& io_service_;
	tcp::socket socket_;

	boost::asio::streambuf recv_buffer_;
	boost::asio::streambuf send_buffer_;

	tcp::endpoint endpoint_;

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

	archived_client(boost::asio::io_service& io_service, archiving::archivable_channel_t& channel, tcp::resolver::iterator endpoint_iterator)
		: io_service_(io_service), socket_(io_service), connected_(false), current_state_(states::connecting), channel_(channel)
	{
		endpoint_ = *endpoint_iterator;
		socket_.async_connect(endpoint_,
				boost::bind(&archived_client::handle_connect, this,
						boost::asio::placeholders::error, ++endpoint_iterator));
	}

	void close()
	{
		io_service_.post(boost::bind(&archived_client::do_close, this));
	}

private:
	void handle_connect(const boost::system::error_code& error, tcp::resolver::iterator endpoint_iterator)
	{
		if (!error) {
			run_statemachine(error);
		}
		else if (endpoint_iterator != tcp::resolver::iterator()) {
			socket_.close();

			endpoint_ = *endpoint_iterator;
			socket_.async_connect(endpoint_,
					boost::bind(&archived_client::handle_connect, this,
							boost::asio::placeholders::error, ++endpoint_iterator));
		}
		else {
			LogCritical("Unable to connect to ArchiveD");
		}
	}

	void run_statemachine(const boost::system::error_code& error)
	{
		if(error) {
			//TODO reconnection & reset logic
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

			break;
		}

		case states::handshake_recv:
		{
			std::string ArchiveD, version_number, protocol, protocol_version_number;
			char v, sp[3], cr, lf;
			std::istream is(&recv_buffer_);
			is.unsetf(std::ios_base::skipws);
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
			io_service_.post(boost::bind(&archived_client::do_close, this));
			break;
		}

		case states::handshake_sent:
		{
			current_state_ = states::handshake_ack_waiting;
			boost::asio::async_read_until(socket_, recv_buffer_, "\r\n",
					boost::bind(&archived_client::run_statemachine, this,
							boost::asio::placeholders::error));
			break;
		}

		case states::handshake_ack_waiting:
		{
			std::string OK_or_ERR, message;
			char sp, cr, lf;
			std::istream is(&recv_buffer_); is.unsetf(std::ios_base::skipws);
			is >> OK_or_ERR;
			if(OK_or_ERR == "ERR") {
				is >> sp; std::getline(is, message, '\r'); is >> lf;
				LogError("handshake_ack_waiting: error: "<<message);
				current_state_ = states::handshake_ack_err;
				io_service_.post(boost::bind(&archived_client::do_close, this));
			}
			else {
				is >> cr >> lf;
				LogMessage("Handshake OK; Ready to PUT");
				current_state_ = states::ready_to_put;
				run_statemachine(error);
			}

			break;
		}

		case states::ready_to_put:
		case states::put_sent_file_ack_ok:
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
			break;
		}

		case states::put_sent_ack_waiting:
		{
			std::string OK_or_ERR, PUT, md5, message;
			char sp[4], cr, lf;
			archiving::org_id OrgID;
			std::istream is(&recv_buffer_); is.unsetf(std::ios_base::skipws);
			is >> OK_or_ERR >> sp[0] >> PUT >> sp[1] >> OrgID >> sp[2] >> md5;
			if(OK_or_ERR == "ERR") {
				is >> sp[3]; std::getline(is, message, '\r'); is >> lf;
				LogError("put_sent_ack_waiting: PUT Error: "<<message);
				current_state_ = states::put_sent_ack_err;
				io_service_.post(boost::bind(&archived_client::do_close, this));
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
			if(put_sent_size_ < put_file_size_) {
				std::streamsize n = std::min<std::streamsize>((std::streamsize)(put_file_size_ - put_sent_size_), (std::streamsize)4096) - send_buffer_.size();

				std::vector<char> buf(n+1);
				n = put_archivable_ptr_->get_fstream_ptr()->rdbuf()->sgetn(&buf[0], n);

				// TODO finetune memory usage here; now we double up unnecessarily!
				// we could read directly into allocated stream space [ after send_buffer_.prepare(n) ]
				send_buffer_.sputn(&buf[0], n);
				send_buffer_.commit(n);

				put_sent_size_ += n;

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
					current_state_ = states::put_sent_file;
					run_statemachine(error);
				}
			}
			else {
				LogError("put_sent_size_ > put_file_size_ : invalid call sequence");
				current_state_ = states::general_err;
				io_service_.post(boost::bind(&archived_client::do_close, this));
			}
			break;
		}

		case states::put_sent_file:
		{
			current_state_ = states::put_sent_file_ack_waiting;
			boost::asio::async_read_until(socket_, recv_buffer_, "\r\n",
					boost::bind(&archived_client::run_statemachine, this,
							boost::asio::placeholders::error));
			break;
		}

		case states::put_sent_file_ack_waiting:
		{
			std::string OK_or_ERR, RECV, md5, message;
			char sp[4], cr, lf;
			archiving::org_id OrgID;
			std::istream is(&recv_buffer_); is.unsetf(std::ios_base::skipws);
			is >> OK_or_ERR >> sp[0] >> RECV >> sp[1] >> OrgID >> sp[2] >> md5;
			if(OK_or_ERR == "ERR") {
				is >> sp[3]; std::getline(is, message, '\r'); is >> lf;
				LogError("put_sent_file_ack_waiting: PUT Error: "<<message);
				current_state_ = states::put_sent_file_ack_err;
				io_service_.post(boost::bind(&archived_client::do_close, this));
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
			io_service_.post(boost::bind(&archived_client::do_close, this));
			current_state_ = states::closing;
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

				if(put_archivable_ptr_.get())
					LogError("Invalid call sequence; put_archivable_ptr_ != NULL");

				io_service_.post(boost::bind(&archived_client::do_close, this));
				current_state_ = states::closing;
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

	void do_close() {
		current_state_ = states::closed;
		socket_.close();
	}
};

}  // namespace app

#endif /* ARCHIVED_CLIENT_HPP_ */
