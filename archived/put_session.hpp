/*
 * put_session.hpp
 *
 *  Created on: 31 Jan 2011
 *      Author: dkorah
 */

#ifndef COLLECTD_SESSION_HPP_
#define COLLECTD_SESSION_HPP_

#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>

#include <rml/logger.h>
#include <rml/utils.h>

#include "../common/defines.h"
#include "../common/archivable.hpp"

#include "globals.h"
#include "types.hpp"

namespace app {

class put_session : public boost::enable_shared_from_this<put_session> {

	struct states {
		enum state_t {
			accepting,
//			accepted,
//			disconnected, TODO

			handshake_sent,
			handshake_recv,
			handshake_err,
			handshake_recv_err,
//			handshake_ack_ok,
//			handshake_ack_err,

			ready_for_put,

			put_received,
//			put_received_processing,
			put_received_ack_ok,
			put_received_ack_err,
			put_receiving_file,
//			put_receiving_file_complete,
//			put_check_received_file,
			put_check_received_file_ack_ok,
			put_check_received_file_ack_err,

			general_err,

			closing,
			closed
		};
	};

	boost::program_options::variables_map& confMap_;

	boost::asio::io_service& io_service_;
	boost::asio::ip::tcp::socket socket_;

	boost::asio::streambuf recv_buffer_;
	boost::asio::streambuf send_buffer_;

	states::state_t current_state_;

	boost::shared_ptr<std::fstream> ofs_;
	archiving::hash_vector md5_;
	archiving::org_id orgID_;
	types::put_metadata::pointer metadata_;
	archiving::archivable::file_type::file_type_t file_type_;
	std::size_t content_length_, xfrd_content_length_;
	bool xfrd_ok_;

	archiving::archivable_channel_t& channel_;

public:
	typedef boost::shared_ptr<put_session> pointer;

	put_session( boost::program_options::variables_map& confMap, archiving::archivable_channel_t& channel, boost::asio::io_service& io_service )
	: confMap_(confMap), io_service_(io_service), socket_(io_service), current_state_(states::accepting), channel_(channel) { }

	boost::asio::ip::tcp::socket& socket() {
		return socket_;
	}

	static void describe_config(boost::program_options::options_description& optsDesc) {
		optsDesc.add_options()
				("archived.cache_dir", boost::program_options::value<std::string>()->default_value("/var/lib/archiving/cache"), "ArchiveD file cache")
				;
	}

	void start() {
		current_state_ = states::handshake_sent;
		std::ostream ostr(&send_buffer_);
		ostr << "ArchiveD v"<<version_number_only_string<<" protocol "<<archiving::put_protocol_version_str<<"\r\n";
		boost::asio::async_write(socket_, send_buffer_,
				boost::bind(
						&put_session::run_statemachine, shared_from_this(),
						boost::asio::placeholders::error));
	}

private:
	void run_statemachine(const boost::system::error_code& error) {
		if(error) {
			//TODO deduce next action from current state & error
			LogError("run_statemachine [current_state:"<<current_state_<<"]: "<<error.message());
			current_state_ = states::general_err;
		}

		switch(current_state_) {

		case states::handshake_sent:
		{
			current_state_ = states::handshake_recv;
			boost::asio::async_read_until(socket_, recv_buffer_, "\r\n",
					boost::bind(&put_session::run_statemachine, shared_from_this(),
							boost::asio::placeholders::error));
			break;
		}

		case states::handshake_recv:
		{
			std::string OK_or_ERR, CollectD, version_number, protocol, protocol_version_number, message;
			char sp1, sp2, sp3, sp4, v, cr, lf;

			std::istream istr(&recv_buffer_);
			istr.unsetf(std::ios_base::skipws);

			std::ostream ostr(&send_buffer_);

			istr >> OK_or_ERR;
			if(OK_or_ERR == "OK") {
				istr >> sp1 >> CollectD >> sp2 >> v >> version_number >> sp3 >> protocol >> sp4 >> protocol_version_number >> cr >> lf;
				if(!((CollectD == "CollectD") && (v == 'v') && (protocol == "protocol"))) {
					LogError("Invalid response from client; aborting");
					current_state_ = states::handshake_err;
					ostr << "ERR invalid response\r\n";
				}
				else if(protocol_version_number != archiving::put_protocol_version_str) {
					LogError("Client protocol version mismatch; aborting");
					current_state_ = states::handshake_err;
					ostr << "ERR protocol mismatch\r\n";
				}
				else {
					if(version_number != version_number_only_string)
						LogWarning("Warning: Client differs in version from server");

					current_state_ = states::ready_for_put;
					ostr << "OK\r\n";
				}

				boost::asio::async_write(socket_, send_buffer_,
						boost::bind(&put_session::run_statemachine, shared_from_this(),
								boost::asio::placeholders::error));
			}
			else if(OK_or_ERR == "ERR") {
				istr >> sp1; std::getline(istr, message, '\r'); istr >> lf;
				LogError("states::handshake_recv: Handshake aborted with error from client:"<<message);
				current_state_ = states::handshake_recv_err;
				run_statemachine(error);
			}
			break;
		}

		case states::accepting:
		case states::closing:
		case states::closed:
		{
			LogError("Invalid state: states::accepting / states::closed / states::closing (state:"<<current_state_<<')');
			current_state_ = states::general_err;
			run_statemachine(error);
			break;
		}

		case states::general_err:
		case states::handshake_err:
		case states::handshake_recv_err:
		case states::put_received_ack_err:
		case states::put_check_received_file_ack_err:
		{
			current_state_ = states::closing;
			io_service_.post(boost::bind(&put_session::do_close, shared_from_this()));
			break;
		}

		case states::ready_for_put:
		case states::put_check_received_file_ack_ok:
		{
			ofs_.reset();
			md5_.clear();
			orgID_ = 0;
			metadata_.reset();
			file_type_ = archiving::archivable::file_type::unknown;
			content_length_ = 0;
			xfrd_content_length_ = 0;
			xfrd_ok_ = false;

			current_state_ = states::put_received;
			boost::asio::async_read_until(socket_, recv_buffer_, "\r\n\r\n",
					boost::bind(&put_session::run_statemachine, shared_from_this(),
							boost::asio::placeholders::error));
			break;
		}

		case states::put_received:
		{
			std::string PUT;
			archiving::archivable::file_type::file_type_t file_type;
			archiving::hash_vector md5;
			archiving::org_id orgID;
			std::size_t content_length;
			char sp1, sp2, sp3, sp4, cr, lf;

			std::istream istr(&recv_buffer_);
			istr.unsetf(std::ios_base::skipws);

			std::ostream ostr(&send_buffer_);

			istr >> PUT >> sp1 >> orgID >> sp2 >> file_type >> sp3 >> md5 >> sp4 >> content_length >> cr >> lf;
			LogMessage("PUT received for Org:"<<orgID<<" file_type:"<<file_type<<" md5:"<<md5<<" content-length:"<<content_length);
			if(!((PUT == "PUT") && ( sp1 == sp2 == sp3 == ' ') && (16 == md5.size()) && (cr == '\r') && (lf == '\n'))) {
				LogError("Invalid PUT request");
				current_state_ = states::put_received_ack_err;
				ostr << "ERR PUT invalid PUT request\r\n";
			}
			else {
				if(content_length <= 0) {
					LogError("Invalid content length:"<<content_length);
					current_state_ = states::put_received_ack_err;
					ostr << "ERR PUT "<<orgID<<' '<<md5<<" invalid content length\r\n";
				}
				else if(orgID <= 0) {
					LogError("Invalid Org ID:"<<orgID);
					current_state_ = states::put_received_ack_err;
					ostr << "ERR PUT "<<orgID<<' '<<md5<<" invalid Org ID\r\n";
				}
				else {
					std::ostringstream os;
					os << "put_org:"<<orgID<<"_md5:"<<md5<<".tmp.";
					rml::utils::tmp_fstream::pointer ofs;
					try {
						ofs = rml::utils::tmp_fstream::pointer(new rml::utils::tmp_fstream(os.str(), content_length, confMap_["archived.cache_dir"].as<std::string>()));
						ofs_ = boost::static_pointer_cast<std::fstream, rml::utils::tmp_fstream>(ofs);
						md5_ = md5;
						orgID_ = orgID;
						metadata_.reset(new types::put_metadata(istr)); // read it from stream
						file_type_ = file_type;
						content_length_ = content_length;
						xfrd_content_length_ = 0;
						xfrd_ok_ = false;
						current_state_ = states::put_received_ack_ok;
						ostr << "OK PUT "<<orgID<<' '<<md5<<"\r\n";
					}
					catch(const rml::exception& e) {
						LogError("states::put_received: rml::exception: "<<e.what());
						current_state_ = states::put_received_ack_err;
						ostr << "ERR PUT "<<orgID<<' '<<md5<<" "<<e.what()<<"\r\n";
					}
					catch(const std::exception& e) {
						LogError("states::put_received: std::exception: "<<e.what());
						current_state_ = states::put_received_ack_err;
						ostr << "ERR PUT "<<orgID<<' '<<md5<<" "<<e.what()<<"\r\n";
					}
				}
			}

			boost::asio::async_write(socket_, send_buffer_,
					boost::bind(&put_session::run_statemachine, shared_from_this(),
							boost::asio::placeholders::error));
			break;
		}

		case states::put_received_ack_ok:
		{
			assert(recv_buffer_.size() == 0);

			current_state_ = states::put_receiving_file;
			boost::asio::async_read(socket_, recv_buffer_,
					boost::bind(&put_session::put_receiving_file_is_complete, shared_from_this(),
							boost::asio::placeholders::error,
							boost::asio::placeholders::bytes_transferred),
					boost::bind(&put_session::run_statemachine, shared_from_this(),
							boost::asio::placeholders::error));
			break;
		}

		case states::put_receiving_file:
		{
			std::ostream ostr(&send_buffer_);
			if(xfrd_ok_) {
				rml::utils::tmp_fstream::pointer ofs = boost::static_pointer_cast<rml::utils::tmp_fstream, std::fstream>(ofs_);
				const archiving::hash_vector& md5 = archiving::md5hasher::hash(*(ofs.get()));
				if(md5 == md5_) {
					ostr << "OK RECV "<<orgID_<<' '<<md5_<<"\r\n";
					current_state_ = states::put_check_received_file_ack_ok;
					ofs->set_persistent();

					channel_.produce(archiving::archivable::pointer(
							new archiving::archivable(ofs->get_filename(), file_type_)));
				}
				else {
					LogError("PUT failed with MD5 mismatch");
					ostr << "ERR RECV "<<orgID_<<' '<<md5_<<" md5 mismatch\r\n";
					current_state_ = states::put_check_received_file_ack_err;
					xfrd_ok_ = false;
				}
			}
			else {
				ostr << "ERR RECV "<<orgID_<<' '<<md5_<<" general failure during transfer\r\n";
				current_state_ = states::put_check_received_file_ack_err;
			}

			ofs_.reset();
			md5_.clear();
			orgID_ = 0;
			file_type_ = archiving::archivable::file_type::unknown;
			content_length_ = 0;
			xfrd_content_length_ = 0;
			xfrd_ok_ = false;

			boost::asio::async_write(socket_, send_buffer_,
					boost::bind(&put_session::run_statemachine, shared_from_this(),
							boost::asio::placeholders::error));
			break;
		}

		}
	}

	bool put_receiving_file_is_complete(const boost::system::error_code& error, std::size_t bytes_transferred) {

		LogVerbose("error: "<<error);
		LogVerbose("bytes_transferred: "<<bytes_transferred);

		if(!error) {

			LogVerbose("content_length_: "<<content_length_);
			LogVerbose("xfrd_content_length_: "<<xfrd_content_length_);

			std::size_t ofs_transferred = 0;

			while(ofs_transferred < bytes_transferred) {
				std::size_t buf_length = std::min<std::size_t>(4096, bytes_transferred - ofs_transferred);
				std::vector<char> buf(buf_length);

				if(content_length_ >= (xfrd_content_length_ + buf_length)) {
					buf_length = recv_buffer_.sgetn(&buf[0], buf_length);
					ofs_->rdbuf()->sputn(&buf[0], buf_length);
					xfrd_content_length_ += buf_length;
					ofs_transferred += buf_length;
				}
				else {
					std::size_t xfr_remaining = content_length_ - xfrd_content_length_;
					if(xfr_remaining) {
						xfr_remaining = recv_buffer_.sgetn(&buf[0], xfr_remaining);
						ofs_->rdbuf()->sputn(&buf[0], xfr_remaining);
						xfrd_content_length_ += xfr_remaining;
						ofs_transferred += xfr_remaining;
					}

					char eof = recv_buffer_.sgetc();
					xfrd_ok_ = (eof == '\x1a');
					ofs_transferred += 1;

					return true;
				}
			}

			return false;
		}

		LogError("Error while receiving PUT data: "<<error.message());
		return true;
	}

	void do_close() {
		current_state_ = states::closed;
		socket_.close();
	}
};

}  // namespace app

#endif /* COLLECTD_SESSION_HPP_ */
