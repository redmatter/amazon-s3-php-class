/*
 * archived_client.hpp
 *
 *  Created on: 29 Jan 2011
 *	  Author: dkorah
 */

#ifndef ARCHIVED_CLIENT_HPP_
#define ARCHIVED_CLIENT_HPP_

#include "archivable.hpp"
#include "defines.h"
#include "types.hpp"
#include "globals.h"
#include "collectd/callbacks.hpp"

#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/atomic.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

namespace app {

using boost::asio::ip::tcp;

class archived_client;
std::ostream &operator<<(std::ostream &os, archived_client const& ac);

class archived_client : public boost::enable_shared_from_this< archived_client >
{
private:
	static boost::atomic_uint n_instances_;
	static boost::atomic_uint n_ok_;
	static boost::atomic_uint instance_id_seq_;
	
	void set_is_archived_ok(const bool ok)
	{
		LogVerbose("ID:"<<instance_id_<<": set_is_archived_ok: "<<ok);
		if(ok) {
			n_ok_++;
		} else {
			n_ok_--;
		}
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

		/**
		 * All states are pretty much interruptable except in certain cases where it can cause duplication
		 * 
		 * @param s
		 * @return
		 */
		static inline bool is_interruptable(const state_t& s)
		{
			return s <= put_sending_file || s >= put_sent_file_ack_err;
		}
	};

	const unsigned int instance_id_;

	boost::asio::io_service& io_service_;
	boost::asio::strand strand_;
	
	boost::asio::ip::tcp::resolver resolver_;
	boost::shared_ptr< tcp::socket> p_socket_;
	unsigned short int local_port_;

	boost::asio::deadline_timer timer_;

	boost::asio::streambuf recv_buffer_;
	boost::asio::streambuf send_buffer_;

	const std::string archived_host_;
	const std::string archived_port_;

	bool connected_;
	state::state_t current_state_;
	int reconnect_delay_;

	const common::collectd::fn_produce_type& fn_produce_;
	const common::collectd::fn_consume_with_timeout_finishing_callback_type& fn_consume_;
	const common::collectd::fn_remove_from_catalogue_type& fn_remove_from_catalogue_;
	const common::collectd::fn_save_to_database_type& fn_save_to_database_;

	archiving::archivable::pointer put_archivable_ptr_;
	size_t put_file_size_, put_sent_size_;

	boost::atomic_bool shutdown_flagged_;

	inline bool is_shutdown_flagged()
	{
		return shutdown_flagged_.load(boost::memory_order_consume);
	}

public:
	inline std::ostream &identify_with_state(std::ostream &os) const
	{
		os << "id:" << instance_id_;

		if (local_port_) {
			os << " port:" << local_port_;
		}

		os << " [" << state::to_string(current_state_) << ']';

		if (put_archivable_ptr_) {
			os << " [" << put_archivable_ptr_ << ']';
		}

		return os;
	}

	typedef boost::shared_ptr<archived_client> pointer;

	static float get_is_archived_ok()
	{
		if(n_instances_ == 0U || n_ok_ == 0U)
			return 0.0;
		
		return (float)n_ok_.load() / (float)n_instances_.load();
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
			const common::collectd::fn_produce_type& fn_produce,
			const common::collectd::fn_consume_with_timeout_finishing_callback_type& fn_consume,
			const common::collectd::fn_remove_from_catalogue_type& fn_remove_from_catalogue,
			const common::collectd::fn_save_to_database_type& fn_save_to_database,
			const std::string& archived_host,
			const std::string& archived_port) :
				instance_id_(++instance_id_seq_),
				io_service_(io_service),
				strand_(io_service_),
				resolver_(io_service_),
				local_port_(0),
				timer_(io_service_),
				archived_host_(archived_host),
				archived_port_(archived_port),
				connected_(false),
				current_state_(state::initialised),
				reconnect_delay_(reconnect_delay_seconds),
				fn_produce_(fn_produce), fn_consume_(fn_consume),
				fn_remove_from_catalogue_(fn_remove_from_catalogue),
				fn_save_to_database_(fn_save_to_database),
				shutdown_flagged_(false)
	{
		n_instances_++;
		LogMessage("archived-client created; ID:"<<instance_id_);
	}

	~archived_client()
	{
		n_instances_--;
		LogMessage("archived-client destroyed; ID:"<<instance_id_);
	}

	static inline unsigned int get_instances_count()
	{
		return n_instances_;
	}

	inline void setup_retry_and_reset_current_archivable(const char *message)
	{
		if(put_archivable_ptr_) {
			const bool &can_reschedule(put_archivable_ptr_->setup_for_retry());
			if (!is_shutdown_flagged() && can_reschedule) {
				// producing into the queue while shutting down will throw queue-finishing exception
				fn_produce_(put_archivable_ptr_, false);
			}

			LogVerbose(*this<<' '<<message);
			put_archivable_ptr_.reset();
		}
	}

	inline void connect_archived()
	{
		if(p_socket_) {
			boost::system::error_code error_ignore;
			p_socket_->cancel(error_ignore);
			p_socket_.reset();

			local_port_ = 0;
		}

		connected_ = false;
		setup_retry_and_reset_current_archivable("Reconnecting while processing");

		boost::asio::ip::tcp::resolver::query query(archived_host_, archived_port_);
		resolver_.cancel();
		resolver_.async_resolve(query,
				strand_.wrap(boost::bind(&archived_client::handle_resolve, shared_from_this(),
						boost::asio::placeholders::error,
						boost::asio::placeholders::iterator)));

		set_reconnect_timer(10, false);
	}

	void reconnect(const bool with_bye = true, int delay_seconds = reconnect_delay_seconds)
	{
		if (!with_bye) {
			do_close(true, static_cast<unsigned int>(delay_seconds));
		}
		else {
			reconnect_delay_ = delay_seconds;
			close(true);
		}
	}

	void close(const bool reconnect = false)
	{
		current_state_ = reconnect? state::reconnecting_bye_wait: state::closing_bye_wait;

		std::ostream ostr(&send_buffer_); ostr.clear();
		ostr << "BYE\r\n\r\n";
		boost::asio::async_write(*p_socket_, send_buffer_,
				strand_.wrap(boost::bind(&archived_client::handle_write, shared_from_this(),
						boost::asio::placeholders::error,
						boost::asio::placeholders::bytes_transferred)));
	}

	void shutdown()
	{
		shutdown_flagged_ = true;
		if (state::is_interruptable(current_state_)) {
			strand_.post(strand_.wrap(
					boost::bind(&archived_client::shutdown_now, shared_from_this(), boost::system::error_code())));
		} else {
			DevDebug(*this<<" shutdown in 5 seconds");
			cancel_timer();
			timer_.expires_from_now(boost::posix_time::seconds(5));
			timer_.async_wait(strand_.wrap(
					boost::bind(&archived_client::shutdown_now, shared_from_this(), boost::asio::placeholders::error)));
		}
	}

private:
	void shutdown_now(const boost::system::error_code& err)
	{
		DevDebug(*this<<" shutdown in progress");

		if (!err) {
			if (put_archivable_ptr_) {
				fn_save_to_database_(put_archivable_ptr_);
			}

			do_close(false);
		}
	}

	inline void cancel_timer()
	{
		DevDebug(*this<<" cancel_timer");
		timer_.cancel();
	}

	inline void set_reconnect_timer(const int seconds, const bool force_reconnect)
	{
		DevDebug(*this<<" set_reconnect_timer - seconds:"<<seconds<<" force_reconnect:"<<force_reconnect);

		cancel_timer();
		timer_.expires_from_now(boost::posix_time::seconds(seconds));
		timer_.async_wait(strand_.wrap(boost::bind(&archived_client::connect_archived, shared_from_this(),
				force_reconnect, true, boost::asio::placeholders::error)));
	}

	inline void connect_archived(const bool force_reconnect, const bool timer_expired, const boost::system::error_code& error) try
	{
		if(timer_expired && (error != boost::asio::error::operation_aborted)) {
			LogMessage(*this<<" Timeout; reconnecting...");
			connect_archived(force_reconnect);
		}
		else if(!timer_expired && (error == boost::asio::error::operation_aborted)) {
			LogMessage(*this<<" Socket operation aborted; reconnecting...");
			set_reconnect_timer(10, true);
		}
	}
	catch(const std::exception& e)
	{
		LogWarning(*this<<" Exception: "<<e.what());
		reconnect(false, 10);
	}
	catch(...)
	{
		LogWarning(*this<<" Exception: Unknown");
		reconnect(false, 10);
	}

	inline void connect_archived(const bool force_reconnect)
	{
		if(connected_ && force_reconnect) {
			do_close(true);
			return;
		}

		// we are here because the timer expired
		else if(!connected_ && (current_state_ == state::initialised))
			LogWarning(*this<<" Archived connection failed; retrying...");

		connect_archived();
	}

	void handle_resolve(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) try
	{
		if (!err) {
			boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
			p_socket_.reset(new tcp::socket(io_service_));
			p_socket_->async_connect(endpoint,
					strand_.wrap(boost::bind(&archived_client::handle_connect, shared_from_this(),
							boost::asio::placeholders::error, ++endpoint_iterator)));
		}
		else {
			LogWarning(*this<<" Error resolving ArchiveD host "<<archived_host_<<" ( port "<<archived_port_<<" ) - error:"<<err.message());
		}
	}
	catch(const std::exception& e)
	{
		LogWarning(*this<<" Exception: "<<e.what());
	}
	catch(...)
	{
		LogWarning(*this<<" Exception: Unknown");
	}

	inline void finalise_current_archivable()
	{
		fn_remove_from_catalogue_(put_archivable_ptr_);
		put_archivable_ptr_->set_done(false);

		// try to save once; if it fails retry in a loop
		if (!fn_save_to_database_(put_archivable_ptr_)) {
			int retry = 0;
			bool saved_ok = false;
			while (!is_shutdown_flagged()) {
				boost::this_thread::sleep_for(boost::chrono::seconds(2));
				LogMessage(*this<<" [retry: "<<++retry<<"] Save states to DB failed; retrying..");
				saved_ok = fn_save_to_database_(put_archivable_ptr_);
				if (saved_ok) {
					break;
				}
			}

			if (!saved_ok && is_shutdown_flagged()) {
				LogWarning(*this<<" Quit flagged during database retry; not saved");
			}
		}

		put_archivable_ptr_.reset();
	}

	void handle_connect(const boost::system::error_code& error, tcp::resolver::iterator endpoint_iterator) try
	{
		if (!error) {
			boost::system::error_code ec;
			boost::asio::socket_base::send_buffer_size option(8192);
			p_socket_->set_option(option, ec);
			if(ec) {
				LogWarning(*this<<" set_option:send_buffer_size failed; err:"<<ec.message());
				run_statemachine(error);
				return;
			}

			current_state_ = state::connecting;
			cancel_timer();

			local_port_ = p_socket_->local_endpoint().port();
			LogMessage(*this<<" Connected to ArchiveD " << archived_host_ << ':' << archived_port_);

			run_statemachine(error);
		}
		else {
			LogWarning(*this<<" Error connecting to ArchiveD host "<<archived_host_<<" ( port "<<archived_port_<<" ) - error:"<<error.message());
			if (endpoint_iterator != tcp::resolver::iterator()) {
				boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
				p_socket_.reset(new tcp::socket(io_service_));
				p_socket_->async_connect(endpoint,
						strand_.wrap(boost::bind(&archived_client::handle_connect, shared_from_this(),
								boost::asio::placeholders::error, ++endpoint_iterator)));
			}
		}
	}
	catch(const std::exception& e)
	{
		LogWarning(*this<<" Exception: "<<e.what());
	}
	catch(...)
	{
		LogWarning(*this<<" Exception: Unknown");
	}

	void handle_write(const boost::system::error_code& error, std::size_t bytes_transferred) try
	{
		if(!error) {
			if(current_state_ == state::nop_sent) {
				current_state_ = state::nop_ack_wait;
				boost::asio::async_read_until(*p_socket_, recv_buffer_, "\r\n",
						strand_.wrap(boost::bind(&archived_client::run_statemachine, shared_from_this(),
								boost::asio::placeholders::error)));

				set_reconnect_timer(10, true);
				return;
			}
		}

		if((current_state_ == state::closing_bye_wait) || (current_state_ == state::reconnecting_bye_wait)) {
			if(!error) {
				boost::asio::async_read_until(*p_socket_, recv_buffer_, "\r\n",
						strand_.wrap(boost::bind(&archived_client::run_statemachine, shared_from_this(),
								boost::asio::placeholders::error)));

				timer_.expires_from_now(boost::posix_time::seconds(2));
				timer_.async_wait(
						strand_.wrap(boost::bind(&archived_client::timer_do_close, shared_from_this(),
								current_state_ == state::reconnecting_bye_wait,
								reconnect_delay_,
								boost::asio::placeholders::error)));
			}
			else
				do_close(current_state_ == state::reconnecting_bye_wait, static_cast<unsigned int>(reconnect_delay_));

			return;
		}

		if(!error)
			LogWarning(*this<<" Invalid current-state");
		else
			LogError(*this<<" Socket IO error: "<<error.message());

		reconnect(false);
	}
	catch(const std::exception& e)
	{
		LogWarning(*this<<" Exception: "<<e.what());
		reconnect(false);
	}
	catch(...)
	{
		LogWarning(*this<<" Exception: Unknown");
		reconnect(false);
	}

	void run_statemachine(const boost::system::error_code& error) try
	{
		// reset any timer in progress; if we loop through, we are alive and kicking
		cancel_timer();

		if(error) {
			LogError(*this<<" run_statemachine - error: "<<error.message());
			
			switch(current_state_) {
			case state::closing_bye_wait:
			case state::reconnecting_bye_wait:
				break;
				
			default:
				current_state_ = state::general_err;
				break;
			}
		}

		switch(current_state_) {
		case state::connecting:
		{
			connected_ = true;

			{
				boost::system::error_code ec;
				tcp::socket::keep_alive option(true);
				p_socket_->set_option(option, ec);
				if(ec)
					run_statemachine(ec);
			}

			current_state_ = state::handshake_recv;

			boost::asio::async_read_until(*p_socket_, recv_buffer_, "\r\n",
					strand_.wrap(boost::bind(&archived_client::run_statemachine, shared_from_this(),
							boost::asio::placeholders::error)));

			set_reconnect_timer(5, true);

			break;
		}

		case state::handshake_recv:
		{
			std::string ArchiveD, version_number, protocol, protocol_version_number;
			char v, lf;

			SetupRecvStream(*this, "ArchiveD", is, recv_buffer_);

			std::ostream ostr(&send_buffer_); ostr.clear();
			if(!((std::getline(is, ArchiveD, ' ') &&
					is.get(v) &&
					std::getline(is, version_number, ' ') &&
					std::getline(is, protocol, ' ') &&
					std::getline(is, protocol_version_number, '\r') &&
					is.get(lf)) &&
					ArchiveD == "ArchiveD" && protocol == "protocol")) {
				ostr << "ERR invalid response\r\n";
				LogError(*this<<" Error: Invalid response from server");
				current_state_ = state::handshake_err;
			}
			else if(protocol_version_number != archiving::put_protocol_version_str) {
				ostr << "ERR protocol version mismatch\r\n";
				LogError(*this<<" Error: Server's protocol version ("<<protocol_version_number<<") not same as "<<archiving::put_protocol_version_str);
				current_state_ = state::handshake_err;
			}
			else {
				if(version_number != version_number_only_string)
					LogWarning(*this<<" Warning: Server differs in version from client");

				ostr << "OK CollectD v"<<version_number_only_string<<" protocol "<<archiving::put_protocol_version_str<<"\r\n";

				current_state_ = state::handshake_sent;
			}

			boost::asio::async_write(*p_socket_, send_buffer_,
					strand_.wrap(boost::bind(&archived_client::run_statemachine, shared_from_this(),
							boost::asio::placeholders::error)));

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
			boost::asio::async_read_until(*p_socket_, recv_buffer_, "\r\n",
					strand_.wrap(boost::bind(&archived_client::run_statemachine, shared_from_this(),
							boost::asio::placeholders::error)));

			set_reconnect_timer(10, true);

			break;
		}

		case state::handshake_ack_waiting:
		{
			std::string OK_or_ERR, message;
			char lf;

			SetupRecvStream(*this, "ArchiveD", is, recv_buffer_);
			if(std::getline(is, OK_or_ERR, ' ') && (OK_or_ERR != "OK\r\n")) {
				std::getline(is, message, '\r'); is.get(lf);
				LogError(*this<<" handshake_ack_waiting: error: "<<message);
				current_state_ = state::handshake_ack_err;

				reconnect(false);
			}
			else {
				LogMessage(*this<<" Handshake OK; Ready to PUT");
				current_state_ = state::ready_to_put;
				set_is_archived_ok(true);
				run_statemachine(error);
			}

			break;
		}

		case state::nop_ack_wait:
		{
			std::string OK_or_ERR, message;
			char lf;

			SetupRecvStream(*this, "ArchiveD", is, recv_buffer_);
			if(std::getline(is, OK_or_ERR, ' ') && (OK_or_ERR != "OK\r\n")) {
				std::getline(is, message, '\r'); is.get(lf);
				LogError(*this<<" NOP Error: "<<message);
				current_state_ = state::nop_ack_err;
				reconnect(true, 5);
			}
			else {
				current_state_ = state::nop_ack_ok;
				run_statemachine(error);
			}

			break;
		}

		case state::put_sent_file_ack_ok:
		{
			if(put_archivable_ptr_) {
				finalise_current_archivable();
			}
		}
		/* no break */
		case state::nop_ack_ok:
		case state::ready_to_put:
		{
			get_archivable_for_put();
			break;
		}

		case state::put_sent_file_ack_err:
		{
			// TODO set retry interval etc
			setup_retry_and_reset_current_archivable("Error while sending");

			reconnect(false);
			break;
		}

		case state::put_sent:
		{
			current_state_ = state::put_sent_ack_waiting;
			boost::asio::async_read_until(*p_socket_, recv_buffer_, "\r\n",
					strand_.wrap(boost::bind(&archived_client::run_statemachine, shared_from_this(),
							boost::asio::placeholders::error)));

			// allow ArchiveD time based on the file-size to do disk allocations etc
			set_reconnect_timer(std::max(static_cast<const int>(put_file_size_ / 102400), 20), true);

			break;
		}

		case state::put_sent_ack_waiting:
		{
			std::string OK_or_ERR, PUT, file_type_str, OrgID_str, uuid, message;
			char cr, lf;

			SetupRecvStream(*this, "ArchiveD", is, recv_buffer_);
			if(!(std::getline(is, OK_or_ERR, ' ') &&
					std::getline(is, PUT, ' ') &&
					std::getline(is, file_type_str, ' ') &&
					std::getline(is, OrgID_str, ' ') &&
					std::getline(is, uuid, ' ') &&

					OK_or_ERR == "OK")) {

				std::getline(is, message, '\r'); is.get(lf);
				LogError(*this<<" put_sent_ack_waiting: PUT Error: "<<message);
				current_state_ = state::put_sent_ack_err;
				reconnect(true);
			}
			else {
				is.get(cr); is.get(lf);
				LogMessage(*this<<" PUT OK; Sending file");

				current_state_ = state::put_sending_file;
				rml::utils::tmp_fstream::pointer ifs = put_archivable_ptr_->get_fstream_ptr();
				ifs->re_open();
				ifs->rdbuf()->pubseekpos(0,std::ios::in);

				put_sent_size_ = 0;
				if(send_buffer_.size()) {
					LogWarning(*this<<" send_buffer_.size() != 0 ["<<send_buffer_.size()<<"]; clearing it!");
					send_buffer_.consume(send_buffer_.size());
				}
				run_statemachine(error);
			}
			break;
		}

		case state::put_sending_file:
		{
			put_send_file(error, 0);
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
			boost::asio::async_read_until(*p_socket_, recv_buffer_, "\r\n",
					strand_.wrap(boost::bind(&archived_client::run_statemachine, shared_from_this(),
							boost::asio::placeholders::error)));

			// wait for one second per 100KB; ArchiveD needs time to compute MD5, and do other housekeeping
			set_reconnect_timer(std::max(static_cast<const int>(put_file_size_ / 102400), 20), true);

			break;
		}

		case state::put_sent_file_ack_waiting:
		{
			std::string OK_or_ERR, RECV, uuid, message, file_type_str, OrgID_str;
			char cr, lf;
			SetupRecvStream(*this, "ArchiveD", is, recv_buffer_);
			if(!(std::getline(is, OK_or_ERR, ' ') &&
					std::getline(is, RECV, ' ') &&
					std::getline(is, file_type_str, ' ') &&
					std::getline(is, OrgID_str, ' ') &&
					std::getline(is, uuid, ' ') &&

					OK_or_ERR == "OK")) {

				std::getline(is, message, '\r'); is.get(lf);
				LogError(*this<<" PUT Error: "<<message);
				current_state_ = state::put_sent_file_ack_err;
			}
			else {
				is.get(cr); is.get(lf);
				LogMessage(*this<<" PUT OK; Get next for PUT");
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
			do_close(true, static_cast<unsigned int>(reconnect_delay_));
			break;
		}

		default:
			LogMessage(*this<<" (current-state) NOT implemented OR Invalid!");
			/* no break */

		case state::general_err:
		{
			reconnect(false, 10);
			break;
		}

		} // switch
	}
	catch(const std::exception& e)
	{
		LogWarning(*this<<" Exception: "<<e.what());
		reconnect(false, 10);
	}
	catch(...)
	{
		LogWarning(*this<<" Exception: Unknown");
		reconnect(false, 10);
	}

	/**
	 * @brief Get another for PUT
	 *
	 * Sets up get_archivable_for_put so that the statemachine can continue
	 */
	inline void get_next_archivable_for_put()
	{
		strand_.post(
				strand_.wrap(
						boost::bind(&archived_client::get_archivable_for_put, shared_from_this())));
	}

	/**
	 * @brief Get an archivable for PUT
	 */
	void get_archivable_for_put() try
	{
		if (put_archivable_ptr_) {
			LogError(*this<<" Invalid call sequence; put_archivable_ptr_ != NULL");
			do_close(true);
			return;
		}

		if (!(current_state_ == state::ready_to_put || current_state_ == state::put_sent_file_ack_ok || current_state_ == state::nop_ack_ok))
		{
			LogError(*this<<" Invalid call sequence; current_state_ is not ready_to_put / put_sent_file_ack_ok / nop_ack_ok");
			do_close(true);
			return;
		}

		put_archivable_ptr_ = fn_consume_(
				false, boost::chrono::milliseconds(10000),
				boost::protect(boost::bind(&archived_client::send_nop_or_reconnect, shared_from_this())),
				boost::protect(boost::bind(&archived_client::close, shared_from_this(), false)));
		// if no archivable was obtained, we would be either send_nop_or_reconnect-ing or close-ing by now
		if (!put_archivable_ptr_) {
			return;
		}

		LogMessage(*this<<" Obtained for PUT");

		// if we are going down, give up!
		if (is_shutdown_flagged()) {
			LogMessage(*this<<" Quit flagged during consume; closing");
			// reset put_archivable_ptr_ so that the archivable does not get re-added into a queue / channel
			// that would be set to be "finishing" by now
			put_archivable_ptr_.reset();
			do_close();
			return;
		}

		// if the socket has disconnected while we were waiting, we need to reconnect
		if (!p_socket_ || !p_socket_->is_open()) {
			LogMessage(*this<<" Socket found unusable after consume; reconnecting");
			reconnect(false);
			return;
		}

		if(put_archivable_ptr_->get_current_state() >= archiving::archivable::state::stored) {
			LogWarning(*this<<" Already stored !!! ???");
			finalise_current_archivable();
			get_next_archivable_for_put();
			return;
		}

		std::ostream ostr(&send_buffer_); ostr.clear();
		if (!put_archivable_ptr_->inject_put_request(ostr)) {
			put_archivable_ptr_.reset(); // so that it would not retry (till the daemon is restarted)
			get_next_archivable_for_put();
			return;
		}

		current_state_ = state::put_sent;
		put_file_size_ = put_archivable_ptr_->get_fstream_ptr()->get_size();
		boost::asio::async_write(*p_socket_, send_buffer_, strand_.wrap(
				boost::bind(&archived_client::run_statemachine, shared_from_this(), boost::asio::placeholders::error)));
	}
	catch(const std::exception& e)
	{
		LogWarning(*this<<" Exception: "<<e.what());
	}
	catch(...)
	{
		LogWarning(*this<<" Exception: Unknown");
	}

	inline void send_nop_or_reconnect()
	{
		// if shutdown is in progress, don't bother with any
		if (is_shutdown_flagged()) {
			return;
		}

		// if the socket looks OK, send NOP
		if (p_socket_ && p_socket_->is_open()) {
			LogMessage(*this<<" Timeout while waiting for item to PUT; Sending NOP to ArchiveD");

			std::ostream ostr(&send_buffer_);
			ostr.clear();
			ostr << "NOP\r\n\r\n";

			current_state_ = state::nop_sent;
			boost::asio::async_write(
					*p_socket_, send_buffer_,
					strand_.wrap(
							boost::bind(
									&archived_client::handle_write, shared_from_this(),
									boost::asio::placeholders::error,
									boost::asio::placeholders::bytes_transferred)));
		// else reconnect
		} else {
			reconnect(false);
		}
	}

	void put_send_file(const boost::system::error_code& error, std::size_t bytes_transferred) try
	{
		DevDebug(*this<<" Sent: "<<put_sent_size_<<" / "<<put_file_size_<<" ("<<bytes_transferred<<" bytes)");

		if (is_shutdown_flagged()) {
			LogMessage(*this<<" Aborting PUT");
			return;
		}

		cancel_timer();

		if(error || ( !error && (put_sent_size_ == put_file_size_) )) {
			if (!error && (put_sent_size_ == put_file_size_)) {
				{
					std::string eof("\x1a");
					boost::asio::write(*p_socket_, boost::asio::buffer(eof, 1));
				}
				LogMessage(*this<<" PUT send file completed OK");
				current_state_ = state::put_sent_file_ok;
			}
			else {
				LogError(*this<<" PUT send file completed with error; err:"<<error.message());
				current_state_ = state::put_sent_file_err;
			}
			
			// post to avoid recursion
			strand_.post(boost::bind(&archived_client::run_statemachine, shared_from_this(), error));
			return;
		}

		std::streamsize buf_length = std::min<std::streamsize>( (std::streamsize)(put_file_size_ - put_sent_size_), (std::streamsize)4096 );

		// TODO finetune memory usage here; now we double up unnecessarily!
		// we could read directly into allocated stream space [ after send_buffer_.prepare(buf_length) ]

		std::vector<char> buf(static_cast<unsigned long>(buf_length + 1));
		buf_length = put_archivable_ptr_->get_fstream_ptr()->rdbuf()->sgetn(&buf[0], buf_length);

		put_sent_size_ += buf_length;

		std::ostream ostr(&send_buffer_); ostr.clear();
		ostr.write(&buf[0], buf_length);

		boost::asio::async_write(*p_socket_, send_buffer_,
				strand_.wrap(boost::bind(&archived_client::put_send_file, shared_from_this(),
						boost::asio::placeholders::error,
						boost::asio::placeholders::bytes_transferred)));

		set_reconnect_timer(20, true);
	}
	catch(const std::exception& e)
	{
		LogWarning(*this<<" Exception: "<<e.what());
		reconnect(false, 5);
	}
	catch(...)
	{
		LogWarning(*this<<" Exception: Unknown");
		reconnect(false, 5);
	}

	void timer_do_close(const bool& reconnect, unsigned int reconnect_delay_seconds, const boost::system::error_code& error) try
	{
		if(error != boost::asio::error::operation_aborted)
			do_close(reconnect, reconnect_delay_seconds);
	}
	catch(const std::exception& e)
	{
		LogWarning(*this<<" Exception: "<<e.what());
		do_close(reconnect, reconnect_delay_seconds);
	}
	catch(...)
	{
		LogWarning(*this<<" Exception: Unknown");
		do_close(reconnect, reconnect_delay_seconds);
	}

	void do_close(const bool& reconnect, unsigned int reconnect_delay_seconds) {
		current_state_ = state::closed;
		connected_ = false;

		set_is_archived_ok(false);

		cancel_timer();
		resolver_.cancel();

		if(p_socket_) {
			boost::system::error_code error_ignore;
			p_socket_->cancel(error_ignore);
			p_socket_.reset();

			local_port_ = 0;
		}

		setup_retry_and_reset_current_archivable("Closing while sending");

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

inline std::ostream &operator<<(std::ostream &os, archived_client const& ac)
{
	return ac.identify_with_state(os);
}

}  // namespace app

#endif /* ARCHIVED_CLIENT_HPP_ */
