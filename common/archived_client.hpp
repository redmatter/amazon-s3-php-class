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
#include "database.hpp"

#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>

namespace app {

using boost::asio::ip::tcp;

class archived_client
{
private:
	static boost::mutex mutex_;
	static bool is_archived_ok_;

	static void set_is_archived_ok(const bool ok)
	{
		boost::unique_lock<boost::mutex> lock(mutex_);
		LogVerbose("set_is_archived_ok: "<<ok);
		is_archived_ok_ = ok;
	}

	enum constants
	{
		reconnect_delay_seconds = 3,
	};

	//TODO optimize with masks for quicker switch case
	struct state
	{
		enum state_t
		{
			initialised,
			connecting,
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

			nop_sent,
			nop_ack_wait,
			nop_ack_ok,
			nop_ack_err,

			general_err,

			closing,
			closing_bye_wait,

			reconnecting,
			reconnecting_bye_wait,

			closed,

			n_states
		};

		static const char* to_string(const state_t& s)
		{
			static const char* str[n_states+1] = {
					"initialised",
					"connecting",
//					"connected",
					"disconnected",

					"handshake_recv",
					"handshake_sent",
					"handshake_err",
					"handshake_ack_waiting",
//					"handshake_ack_ok",
					"handshake_ack_err",

					"ready_to_put",

					"put_sent",
					"put_sent_ack_waiting",
					"put_sent_ack_ok",
					"put_sent_ack_err",
					"put_sending_file",
//					"put_sent_file",
					"put_sent_file_ok",
					"put_sent_file_err",
					"put_sent_file_ack_waiting",
					"put_sent_file_ack_ok",
					"put_sent_file_ack_err",

					"nop_sent",
					"nop_ack_wait",
					"nop_ack_ok",
					"nop_ack_err",

					"general_err",

					"closing",
					"closing_bye_wait",

					"reconnecting",
					"reconnecting_bye_wait",

					"closed",
			};

			return str[(s < 0 || s >= n_states)? n_states: s];
		}
	};

	boost::asio::io_service& io_service_;
	boost::asio::ip::tcp::resolver resolver_;
	tcp::socket socket_;
	unsigned short int local_port_;

	boost::asio::deadline_timer timer_;

	boost::asio::streambuf recv_buffer_;
	boost::asio::streambuf send_buffer_;

	const std::string archived_host_;
	const std::string archived_port_;

	bool connected_;
	state::state_t current_state_;
	int reconnect_delay_;

	archiving::archivable_channel_t& channel_;
	archiving::archivable_busy_set_t& archivable_busy_set_;

	archiving::archivable::pointer put_archivable_ptr_;
	size_t put_file_size_, put_sent_size_;

	database::internal& internal_db_;

	volatile bool& quit_;

public:
	typedef boost::shared_ptr<archived_client> pointer;

	static bool get_is_archived_ok()
	{
		boost::unique_lock<boost::mutex> lock(mutex_);
		return is_archived_ok_;
	}

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
			archiving::archivable_busy_set_t& archivable_busy_set,
			const std::string& archived_host,
			const std::string& archived_port,
			database::internal& internal_db,
			volatile bool& quit) :
				io_service_(io_service),
				resolver_(io_service_),
				socket_(io_service_),
				local_port_(0),
				timer_(io_service_),
				archived_host_(archived_host),
				archived_port_(archived_port),
				connected_(false),
				current_state_(state::initialised),
				reconnect_delay_(reconnect_delay_seconds),
				channel_(channel),
				archivable_busy_set_(archivable_busy_set),
				internal_db_(internal_db),
				quit_(quit)
	{
		connect_archived();
	}

	void reconnect(const bool with_bye = true, int delay_seconds = reconnect_delay_seconds)
	{
		if (!with_bye) {
			io_service_.post(boost::bind(&archived_client::do_close, this, true, delay_seconds));
		}
		else {
			reconnect_delay_ = delay_seconds;
			close(true);
		}
	}

	void close(const bool reconnect = false)
	{
		// io_service_.post(boost::bind(&archived_client::do_close, this));

		current_state_ = reconnect? state::reconnecting_bye_wait: state::closing_bye_wait;

		std::ostream ostr(&send_buffer_); ostr.clear();
		ostr << "BYE\r\n\r\n";
		boost::asio::async_write(socket_, send_buffer_,
				boost::bind(&archived_client::handle_write, this,
						boost::asio::placeholders::error,
						boost::asio::placeholders::bytes_transferred));
	}

private:
	inline void set_reconnect_timer(const int seconds, const bool force_reconnect)
	{
		timer_.cancel();
		timer_.expires_from_now(boost::posix_time::seconds(seconds));
		timer_.async_wait(boost::bind(&archived_client::connect_archived, this,
				force_reconnect, true, boost::asio::placeholders::error));
	}

	inline void connect_archived(const bool force_reconnect, const bool timer_expired, const boost::system::error_code& error)
	{
		if(timer_expired && (error != boost::asio::error::operation_aborted)) {
			LogMessage("Timeout; reconnecting...");
			connect_archived(force_reconnect);
		}
		else if(!timer_expired && (error == boost::asio::error::operation_aborted)) {
			LogMessage("Socket operation aborted; reconnecting...");
			set_reconnect_timer(10, true);
		}
	}

	inline void connect_archived(const bool force_reconnect)
	{
		if(connected_ && force_reconnect) {
			do_close(true);
			return;
		}

		// we are here because the timer expired
		else if(!connected_ && (current_state_ == state::initialised))
			LogWarning("Archived connection failed; retrying...");

		connect_archived();
	}

	inline void connect_archived()
	{
		if(socket_.is_open()) {
			socket_.cancel();
			socket_.close();
		}

		connected_ = false;
		if(put_archivable_ptr_)
			archivable_busy_set_.remove(put_archivable_ptr_);
		put_archivable_ptr_.reset();

		boost::asio::ip::tcp::resolver::query query(archived_host_, archived_port_);
		resolver_.cancel();
		resolver_.async_resolve(query,
				boost::bind(&archived_client::handle_resolve, this,
						boost::asio::placeholders::error,
						boost::asio::placeholders::iterator));

		set_reconnect_timer(10, false);
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
			boost::asio::socket_base::send_buffer_size option(8192);
			socket_.set_option(option);

			current_state_ = state::connecting;
			timer_.cancel();

			local_port_ = socket_.local_endpoint().port();

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

	void handle_write(const boost::system::error_code& error, std::size_t bytes_transferred)
	{
		if(!error) {
			if(current_state_ == state::nop_sent) {
				current_state_ = state::nop_ack_wait;
				boost::asio::async_read_until(socket_, recv_buffer_, "\r\n",
						boost::bind(&archived_client::run_statemachine, this,
								boost::asio::placeholders::error));

				set_reconnect_timer(5, true);
				return;
			}
		}

		if((current_state_ == state::closing_bye_wait) || (current_state_ == state::reconnecting_bye_wait)) {
			if(!error) {
				boost::asio::async_read_until(socket_, recv_buffer_, "\r\n",
						boost::bind(&archived_client::run_statemachine, this,
								boost::asio::placeholders::error));

				timer_.expires_from_now(boost::posix_time::seconds(2));
				timer_.async_wait(boost::bind(&archived_client::do_close, this, current_state_ == state::reconnecting_bye_wait, reconnect_delay_));
			}
			else
				io_service_.post(boost::bind(&archived_client::do_close, this, current_state_ == state::reconnecting_bye_wait, reconnect_delay_));

			return;
		}

		if(!error)
			LogWarning("Invalid current_state:"<<state::to_string(current_state_));
		else
			LogError("Socket IO error: "<<error.message());

		reconnect(false);
	}

	void run_statemachine(const boost::system::error_code& error)
	{
		// reset any timer in progress; if we loop through, we are alive and kicking
		timer_.cancel();

		if(error) {
			LogError("run_statemachine [current_state:"<<state::to_string(current_state_)<<"]: "<<error.message());
			current_state_ = state::general_err;
		}

		switch(current_state_) {
		case state::connecting:
		{
			connected_ = true;

			{
				tcp::socket::keep_alive option(true);
				socket_.set_option(option);
			}

			current_state_ = state::handshake_recv;

			boost::asio::async_read_until(socket_, recv_buffer_, "\r\n",
					boost::bind(&archived_client::run_statemachine, this,
							boost::asio::placeholders::error));

			set_reconnect_timer(2, true);

			break;
		}

		case state::handshake_recv:
		{
			std::string ArchiveD, version_number, protocol, protocol_version_number;
			char v, sp[3], cr, lf;

			SetupRecvStream("ArchiveD", is, recv_buffer_);

			is >> ArchiveD >> sp[0] >> v >> version_number >> sp[1] >> protocol >> sp[2] >> protocol_version_number >> cr >> lf;

			std::ostream ostr(&send_buffer_); ostr.clear();

			if (ArchiveD != "ArchiveD" || protocol != "protocol") {
				ostr << "ERR invalid response\r\n";
				LogError("Error: Invalid response from server");
				current_state_ = state::handshake_err;
			}
			else if(protocol_version_number != archiving::put_protocol_version_str) {
				ostr << "ERR protocol version mismatch\r\n";
				LogError("Error: Server's protocol version ("<<protocol_version_number<<") not same as "<<archiving::put_protocol_version_str);
				current_state_ = state::handshake_err;
			}
			else {
				if(version_number != version_number_only_string)
					LogWarning("Warning: Server differs in version from client");

				ostr << "OK CollectD v"<<version_number_only_string<<" protocol "<<archiving::put_protocol_version_str<<"\r\n";

				current_state_ = state::handshake_sent;
			}

			boost::asio::async_write(socket_, send_buffer_,
					boost::bind(&archived_client::run_statemachine, this,
							boost::asio::placeholders::error));

			break;
		}


		case state::handshake_err:
		{
			reconnect(false);
			break;
		}

		case state::handshake_sent:
		{
			current_state_ = state::handshake_ack_waiting;
			boost::asio::async_read_until(socket_, recv_buffer_, "\r\n",
					boost::bind(&archived_client::run_statemachine, this,
							boost::asio::placeholders::error));

			set_reconnect_timer(2, true);

			break;
		}

		case state::handshake_ack_waiting:
		{
			std::string OK_or_ERR, message;
			char sp, cr, lf;

			SetupRecvStream("ArchiveD", is, recv_buffer_);

			is >> OK_or_ERR;
			if(OK_or_ERR == "ERR") {
				is >> sp; std::getline(is, message, '\r'); is >> lf;
				LogError("handshake_ack_waiting: error: "<<message);
				current_state_ = state::handshake_ack_err;

				reconnect(false);
			}
			else {
				is >> cr >> lf;
				LogMessage(local_port_<<" Handshake OK; Ready to PUT");
				current_state_ = state::ready_to_put;
				set_is_archived_ok(true);
				run_statemachine(error);
			}

			break;
		}

		case state::nop_ack_wait:
		{
			std::string OK_or_ERR, message;
			char sp, cr, lf;

			SetupRecvStream("ArchiveD", is, recv_buffer_);
			is >> OK_or_ERR;
			if(OK_or_ERR != "OK") {
				is >> sp; std::getline(is, message, '\r'); is >> lf;
				LogError("NOP Error: "<<message);
				current_state_ = state::nop_ack_err;
				reconnect(true, 5);
			}
			else {
				is >> cr >> lf;
				current_state_ = state::nop_ack_ok;
				run_statemachine(error);
			}

			break;
		}

		case state::put_sent_file_ack_ok:
		{
			if(put_archivable_ptr_) {
				archivable_busy_set_.remove(put_archivable_ptr_);
				put_archivable_ptr_->set_done(false);
				{
					int retry = 0;
					while(!internal_db_.save(put_archivable_ptr_) && !quit_) {
						LogMessage(local_port_<<' '<<put_archivable_ptr_<<" [retry: "<<retry<<"] Save states to DB failed; retrying..");
						boost::this_thread::sleep(boost::posix_time::seconds(15));
						retry++;
					}

					if(retry && app::quit)
						throw rml::exception("Quit flagged during database retry");
				}
			}

			put_archivable_ptr_.reset();
		}
		case state::nop_ack_ok:
		case state::ready_to_put:
		{
			io_service_.post(boost::bind(&archived_client::get_archivable_for_put, this));
			break;
		}

		case state::put_sent_file_ack_err:
		{
			// TODO set retry interval etc
			/*
			if(put_archivable_ptr_) {
				int retry = 0;
				while(!internal_db_.save(put_archivable_ptr_) && !quit_) {
					LogMessage(put_archivable_ptr_<<" [retry: "<<retry<<"] Save states to DB failed; retrying..");
					boost::this_thread::sleep(boost::posix_time::seconds(15));
					retry++;
				}
			}
			*/

			// reset here so that the item doesnt get removed by do_close()
			archivable_busy_set_.remove(put_archivable_ptr_);
			put_archivable_ptr_.reset();
			reconnect(true);
			break;
		}

		case state::put_sent:
		{
			current_state_ = state::put_sent_ack_waiting;
			boost::asio::async_read_until(socket_, recv_buffer_, "\r\n",
					boost::bind(&archived_client::run_statemachine, this,
							boost::asio::placeholders::error));

			set_reconnect_timer(10, true);

			break;
		}

		case state::put_sent_ack_waiting:
		{
			std::string OK_or_ERR, PUT, md5, message;
			char sp[4], cr, lf;
			archiving::org_id_t OrgID;

			SetupRecvStream("ArchiveD", is, recv_buffer_);

			is >> OK_or_ERR >> sp[0] >> PUT >> sp[1] >> OrgID >> sp[2] >> md5;
			if(OK_or_ERR == "ERR") {
				is >> sp[3]; std::getline(is, message, '\r'); is >> lf;
				LogError(local_port_<<' '<<put_archivable_ptr_<<" put_sent_ack_waiting: PUT Error: "<<message);
				current_state_ = state::put_sent_ack_err;
				reconnect(true);
			}
			else {
				is >> cr >> lf;
				LogMessage(local_port_<<' '<<put_archivable_ptr_<<" PUT OK; Sending file");

				current_state_ = state::put_sending_file;
				rml::utils::tmp_fstream::pointer ifs = put_archivable_ptr_->get_fstream_ptr();
				ifs->re_open();
				ifs->rdbuf()->pubseekpos(0,std::ios::in);

				put_file_size_ = ifs->get_size();
				put_sent_size_ = 0;
				if(send_buffer_.size()) {
					LogWarning(local_port_<<' '<<put_archivable_ptr_<<" send_buffer_.size() != 0 ["<<send_buffer_.size()<<"]; clearing it!");
					send_buffer_.consume(send_buffer_.size());
				}
				run_statemachine(error);
			}
			break;
		}

		case state::put_sending_file:
		{
			io_service_.post(boost::bind(&archived_client::put_send_file, this, error, 0));
			break;
		}

		case state::put_sent_file_err:
		{
			reconnect(true, 10);
			break;
		}

		case state::put_sent_file_ok:
		{
			current_state_ = state::put_sent_file_ack_waiting;
			boost::asio::async_read_until(socket_, recv_buffer_, "\r\n",
					boost::bind(&archived_client::run_statemachine, this,
							boost::asio::placeholders::error));

			set_reconnect_timer(10, true);

			break;
		}

		case state::put_sent_file_ack_waiting:
		{
			std::string OK_or_ERR, RECV, uuid, message, file_type_str;
			char sp[4], cr, lf;
			archiving::org_id_t OrgID;

			SetupRecvStream("ArchiveD", is, recv_buffer_);

			is >> OK_or_ERR >> sp[0] >> RECV >> sp[1] >> file_type_str >> sp[2] >> OrgID >> sp[3] >> uuid;
			LogDebug(local_port_<<' '<<put_archivable_ptr_<<" OK_or_ERR: "<<OK_or_ERR);
			if(OK_or_ERR != "OK") {
				is >> sp[3]; std::getline(is, message, '\r'); is >> lf;
				LogError(local_port_<<' '<<put_archivable_ptr_<<" PUT Error: "<<message);
				current_state_ = state::put_sent_file_ack_err;
			}
			else {
				is >> cr >> lf;
				LogMessage(local_port_<<' '<<put_archivable_ptr_<<" PUT OK; Get next for PUT");
				current_state_ = state::put_sent_file_ack_ok;
			}

			put_archivable_ptr_->hibernate();
			run_statemachine(error);

			break;
		}

		case state::closing_bye_wait:
		{
			current_state_ = state::closing;
			do_close();
			break;
		}

		case state::reconnecting_bye_wait:
		{
			current_state_ = state::reconnecting;
			do_close(true, reconnect_delay_);
			break;
		}

		default:
			LogError(local_port_<<' '<<"State ["<<state::to_string(current_state_)<<"] NOT implemented OR Invalid!");

		case state::general_err:
		{
			reconnect(false, 10);
			break;
		}

		} // switch
	}

	void get_archivable_for_put()
	{
		try {
			if(!(current_state_ == state::ready_to_put || current_state_ == state::put_sent_file_ack_ok || current_state_ == state::nop_ack_ok) || put_archivable_ptr_.get()) {

				if(!(current_state_ == state::ready_to_put || current_state_ == state::put_sent_file_ack_ok || current_state_ == state::nop_ack_ok))
					LogError(local_port_<<' '<<"Invalid call sequence [current_state_:"<<state::to_string(current_state_)<<"]; current_state_ == states::ready_to_put || current_state_ == states::put_sent_file_ack_ok || current_state_ == state::nop_ack_ok");

				if(put_archivable_ptr_)
					LogError(local_port_<<' '<<"Invalid call sequence [current_state_:"<<state::to_string(current_state_)<<"]; put_archivable_ptr_ != NULL");

				io_service_.post(boost::bind(&archived_client::do_close, this, true));
				return;
			}

			put_archivable_ptr_ = channel_.consume(10/*seconds*/);
			LogMessage(local_port_<<' '<<put_archivable_ptr_<<" Got from "<<channel_);

			if(put_archivable_ptr_->get_current_state() >= archiving::archivable::state::stored) {
				LogWarning(local_port_<<' '<<put_archivable_ptr_<<" Already stored !!! ???");

				archivable_busy_set_.remove(put_archivable_ptr_);
				put_archivable_ptr_->set_done(false);
				{
					int retry = 0;
					while(!internal_db_.save(put_archivable_ptr_) && !quit_) {
						LogMessage(local_port_<<' '<<put_archivable_ptr_<<" [retry: "<<retry<<"] Save states to DB failed; retrying..");
						boost::this_thread::sleep(boost::posix_time::seconds(15));
						retry++;
					}

					if(retry && app::quit)
						throw rml::exception("Quit flagged during database retry");
				}

				put_archivable_ptr_.reset();

				io_service_.post(boost::bind(&archived_client::get_archivable_for_put, this));

				return;
			}


			std::ostream ostr(&send_buffer_); ostr.clear();
			put_archivable_ptr_->shove_put_request(ostr);

			current_state_ = state::put_sent;
			boost::asio::async_write(socket_, send_buffer_,
					boost::bind(&archived_client::run_statemachine, this,
							boost::asio::placeholders::error));
		}
		catch(const archiving::archivable_channel_t::consume_timed_out_exception& e) {
			LogDebug(local_port_<<' '<<channel_<<' '<<e.what()<<"; NOP to ArchiveD");
			std::ostream ostr(&send_buffer_); ostr.clear();
			ostr << "NOP\r\n\r\n";
			current_state_ = state::nop_sent;
			boost::asio::async_write(socket_, send_buffer_,
					boost::bind(&archived_client::handle_write, this,
							boost::asio::placeholders::error,
							boost::asio::placeholders::bytes_transferred));
		}
		catch(const archiving::archivable_channel_t::finishing_exception& e) {
			LogMessage("channel finishing: closing");
			close();
		}
		catch(const std::exception& e) {
			LogWarning(e.what());
		}
	}

	void put_send_file(const boost::system::error_code& error, std::size_t bytes_transferred)
	{
		LogDebug(local_port_<<' '<<put_archivable_ptr_<<" Sent: "<<put_sent_size_<<" / "<<put_file_size_<<" ("<<bytes_transferred<<" bytes)");

		if(error || ( !error && (put_sent_size_ == put_file_size_) )) {
			if (!error && (put_sent_size_ == put_file_size_)) {
				{
					std::string eof("\x1a");
					boost::asio::write(socket_, boost::asio::buffer(eof, 1));
				}
				LogMessage(local_port_<<' '<<put_archivable_ptr_<<" PUT send file completed OK");
				current_state_ = state::put_sent_file_ok;
			}
			else {
				LogError(local_port_<<' '<<put_archivable_ptr_<<" PUT send file completed with error; err:"<<error.message());
				current_state_ = state::put_sent_file_err;
			}
			io_service_.post(boost::bind(&archived_client::run_statemachine, this, error));
			return;
		}

		std::streamsize buf_length = std::min<std::streamsize>( (std::streamsize)(put_file_size_ - put_sent_size_), (std::streamsize)4096 );

		// TODO finetune memory usage here; now we double up unnecessarily!
		// we could read directly into allocated stream space [ after send_buffer_.prepare(buf_length) ]

		std::vector<char> buf(buf_length + 1);
		buf_length = put_archivable_ptr_->get_fstream_ptr()->rdbuf()->sgetn(&buf[0], buf_length);

		put_sent_size_ += buf_length;

		std::ostream ostr(&send_buffer_); ostr.clear();
		ostr.write(&buf[0], buf_length);

		boost::asio::async_write(socket_, send_buffer_,
				boost::bind(&archived_client::put_send_file, this,
						boost::asio::placeholders::error,
						boost::asio::placeholders::bytes_transferred));
	}

	void do_close(const bool& reconnect, unsigned int reconnect_delay_seconds) {
		current_state_ = state::closed;
		connected_ = false;

		set_is_archived_ok(false);

		timer_.cancel();
		resolver_.cancel();
		if(socket_.is_open()) {
			socket_.cancel();
			socket_.close();
		}

		if(put_archivable_ptr_)
			archivable_busy_set_.remove(put_archivable_ptr_);

		put_archivable_ptr_.reset();

		if(reconnect)
			set_reconnect_timer(reconnect_delay_seconds, false);
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
