/*
 * put_session.hpp
 *
 *  Created on: 31 Jan 2011
 *      Author: dkorah
 */

#ifndef COLLECTD_SESSION_HPP_
#define COLLECTD_SESSION_HPP_

#include "globals.h"
#include "types.hpp"
#include "operation_data.hpp"
#include "storage_endpoint.hpp"
#include "clear_cache.hpp"
#include "database.hpp"
#include "cdrmunch_database.hpp"
#include "whisper_client.hpp"

#include "../common/defines.h"
#include "../common/archivable.hpp"
#include "../common/policy.hpp"

#include <rml/logger.h>
#include <rml/utils.h>

#include <istream>
#include <iterator>
#include <map>

#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <boost/format.hpp>

namespace app {

class put_session : public boost::enable_shared_from_this<put_session>
{
	struct protocol_exception : public rml::exception
	{
		protocol_exception(const std::string& message) : rml::exception(message)
		{
		}
	};

	struct states
	{
		enum state_t
		{
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
			put_receiving_file_ok,
			put_receiving_file_err,
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

	policy::cached_finder_t& cached_policy_finder_;
	storage::cached_endpoint_finder_t& cached_endpoint_finder_;

	database::internal& internal_db_;
	database::cdrmunch& cdrmunch_db_;
	clear_cache& clear_cache_;

	boost::asio::ip::tcp::socket socket_;
	std::string remote_ep_;

	boost::asio::streambuf recv_buffer_;
	boost::asio::streambuf send_buffer_;

	states::state_t current_state_;

	rml::utils::tmp_fstream::pointer ofs_;
	archiving::hash_vector md5_;
	std::string resource_id_;
	std::string uuid_;
	archiving::org_id_t orgID_;
	types::metadata::pointer metadata_;
	types::data_sub_format::data_sub_format_t data_type_;
	policy::id_type policy_id_;
	time_t creation_time_;
	std::string creation_date_yyyymmdd_;
	archiving::archivable::file_type::file_type_t file_type_;
	std::size_t content_length_, xfrd_content_length_;
	bool xfrd_ok_;

	operation_data::channel_t& channel_;

	whisper::channel_t& whisper_channel_;

public:
	typedef boost::shared_ptr<put_session> pointer;

	put_session(
			boost::program_options::variables_map& confMap,
			operation_data::channel_t& channel,
			whisper::channel_t& whisper_channel,
			policy::cached_finder_t& cached_policy_finder,
			storage::cached_endpoint_finder_t& cached_endpoint_finder,
			database::internal& internal_db,
			database::cdrmunch& cdrmunch_db,
			clear_cache& clear_cache,
			boost::asio::io_service& io_service ) :
				confMap_(confMap),
				io_service_(io_service),
				cached_policy_finder_(cached_policy_finder),
				cached_endpoint_finder_(cached_endpoint_finder),
				internal_db_(internal_db),
				cdrmunch_db_(cdrmunch_db),
				clear_cache_(clear_cache),
				socket_(io_service),
				current_state_(states::accepting),
				channel_(channel),
				whisper_channel_(whisper_channel)
	{
		//ctor
	}

	boost::asio::ip::tcp::socket& socket()
	{
		return socket_;
	}

	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("archived.cache_dir", boost::program_options::value<std::string>()->default_value("/var/lib/archiving/cache"), "ArchiveD file cache")
				;
	}

	void start()
	{
		boost::asio::socket_base::receive_buffer_size option(16384);
		socket_.set_option(option);

		{
			std::ostringstream ostr;

			boost::system::error_code ec;
			boost::asio::ip::tcp::resolver resolver(io_service_);
			boost::asio::ip::tcp::resolver::iterator itr = resolver.resolve(socket_.remote_endpoint(), ec);
			if(!ec) {
				std::string fqdn((*itr).host_name());
				std::istringstream istr(fqdn);
				std::string hostname;
				std::getline(istr, hostname, '.');

				ostr << hostname <<':'<<socket_.remote_endpoint().port();
			}
			else
				ostr << socket_.remote_endpoint();

			remote_ep_ = ostr.str();
		}

		current_state_ = states::handshake_sent;
		std::ostream ostr(&send_buffer_);
		ostr << "ArchiveD v"<<version_number_only_string<<" protocol "<<archiving::put_protocol_version_str<<"\r\n";
		boost::asio::async_write(socket_, send_buffer_,
				boost::bind(
						&put_session::run_statemachine, shared_from_this(),
						boost::asio::placeholders::error));
	}

private:
	void run_statemachine(const boost::system::error_code& error)
	{
		if(error) {
			//TODO deduce next action from current state & error
			LogError(remote_ep_<<" run_statemachine [current_state:"<<current_state_<<"]: "<<error.message());
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

			SetupRecvStream("CollectD", istr, recv_buffer_);

			std::ostream ostr(&send_buffer_);

			istr >> OK_or_ERR;
			if(OK_or_ERR == "OK") {
				istr >> sp1 >> CollectD >> sp2 >> v >> version_number >> sp3 >> protocol >> sp4 >> protocol_version_number >> cr >> lf;
				if(!((CollectD == "CollectD") && (v == 'v') && (protocol == "protocol"))) {
					LogError(remote_ep_<<" Invalid response from client; aborting");
					current_state_ = states::handshake_err;
					ostr << "ERR invalid response\r\n";
				}
				else if(protocol_version_number != archiving::put_protocol_version_str) {
					LogError(remote_ep_<<" Client protocol version mismatch; aborting");
					current_state_ = states::handshake_err;
					ostr << "ERR protocol mismatch\r\n";
				}
				else {
					if(version_number != version_number_only_string)
						LogWarning(remote_ep_<<" Warning: Client differs in version from server");

					current_state_ = states::ready_for_put;
					ostr << "OK\r\n";
				}

				boost::asio::async_write(socket_, send_buffer_,
						boost::bind(&put_session::run_statemachine, shared_from_this(),
								boost::asio::placeholders::error));
			}
			else if(OK_or_ERR == "ERR") {
				istr >> sp1; std::getline(istr, message, '\r'); istr >> lf;
				LogError(remote_ep_<<" states::handshake_recv: Handshake aborted with error from client:"<<message);
				current_state_ = states::handshake_recv_err;
				run_statemachine(error);
			}
			break;
		}

		case states::accepting:
		case states::closing:
		case states::closed:
		{
			LogError(remote_ep_<<" Invalid state: states::accepting / states::closed / states::closing (state:"<<current_state_<<')');
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
			md5_.assign((byte)0);
			resource_id_.clear();
			uuid_.clear();
			orgID_ = 0;
			metadata_.reset();
			data_type_ = types::data_sub_format::unknown;
			file_type_ = archiving::archivable::file_type::unknown;
			content_length_ = 0;
			policy_id_ = 0;
			creation_time_ = 0;
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
			std::string verb;
			archiving::archivable::file_type::file_type_t file_type;
			archiving::org_id_t orgID;
			std::size_t content_length;
			std::string uuid;
			char sp1, sp2, sp3, sp4, cr, lf;

			std::ostream ostr(&send_buffer_);

			std::istream istr(&recv_buffer_);
			istr.unsetf(std::ios_base::skipws);

			istr >> verb;
			if(!(verb == "NOP" || verb == "PUT" || verb == "BYE")) {
				std::string str;
				std::getline(istr, str, '\r'); istr >> lf;

				LogError(remote_ep_<<" Invalid request received '"<<verb<<str<<'\'');
				current_state_ = states::put_received_ack_err;
				ostr << "ERR invalid request\r\n";
			}
			else {
				if(verb == "NOP" || verb == "BYE") {
					istr >> cr >> lf >> cr >> lf;
					LogDebug(remote_ep_<<' '<<verb<<" received");
					ostr << "OK\r\n";
					if(verb == "BYE") {
						LogMessage(remote_ep_<<' '<<verb<<" received; Closing");
						current_state_ = states::closing;
						io_service_.post(boost::bind(&put_session::do_close, shared_from_this()));
					}
					else
						current_state_ = states::ready_for_put;
				}
				else {
					// PUT ....
					istr >> sp1 >> file_type >> sp2 >> orgID >> sp3 >> uuid >> sp4 >> content_length >> cr >> lf;
					if(!istr || !(( sp1 == ' ' ) && ( sp2 == ' ' ) && ( sp3 == ' ' ) && ( sp4 == ' ' ) && (36 == uuid.size()) && (cr == '\r') && (lf == '\n'))) {
						LogError(remote_ep_<<" Invalid PUT request");
						current_state_ = states::put_received_ack_err;
						ostr << "ERR PUT invalid PUT request\r\n";
					}
					else {
						LogMessage(remote_ep_<<" PUT received [ file_type:"<<file_type<<" Org:"<<orgID<<" uuid:"<<uuid<<" content-length:"<<content_length<<" ]");
						if(content_length <= 0) {
							LogError(remote_ep_<<" Invalid content length:"<<content_length);
							current_state_ = states::put_received_ack_err;
							ostr << "ERR PUT "<<file_type<<' '<<orgID<<' '<<uuid<<" invalid content length\r\n";
						}
						else if(orgID <= 0) {
							LogError(remote_ep_<<" Invalid Org ID:"<<orgID);
							current_state_ = states::put_received_ack_err;
							ostr << "ERR PUT "<<file_type<<' '<<orgID<<' '<<uuid<<" invalid Org ID\r\n";
						}
						else {
							try {

								// Get resource ID
								{
									std::ostringstream os;
									os << file_type<<'/'<<orgID<<'/'<<uuid;
									resource_id_ = os.str();
								}

								// read metadata from stream
								metadata_.reset(new types::metadata(istr));

								// make sure we have CreateTime in headers
								{
									types::metadata::const_iterator itr = metadata_->find("CreateTime");
									if(itr == metadata_->end())
										throw rml::exception("No CreateTime given");

									creation_time_ = rml::utils::strtoull(itr->second.c_str());
								}

								creation_date_yyyymmdd_.clear();
								{
									char str[10];
									struct tm * timeinfo = localtime(&creation_time_);
									strftime(str, 10, "%Y%m%d", timeinfo);

									creation_date_yyyymmdd_ = str;
								}

								// make sure we have MD5 in headers
								{
									types::metadata::const_iterator itr = metadata_->find("MD5");
									if(itr == metadata_->end())
										throw rml::exception("No MD5 given");

									std::istringstream istr(itr->second);
									istr >> md5_;
								}

								// make sure we have PolicyID in headers
								{
									types::metadata::const_iterator itr = metadata_->find("RetentionPolicyID");
									if(itr == metadata_->end())
										throw rml::exception("No RetentionPolicyID given");

									policy_id_ = rml::utils::strtoll(itr->second.c_str());
									if( policy_id_ != policy::pseudo_id::do_not_archive ) {
										// make sure we can handle this PolicyID before CollectD start sending data
										if(!cached_policy_finder_(std::make_pair(orgID, policy_id_))) {
											cached_policy_finder_.forget(std::make_pair(orgID, policy_id_));
											throw protocol_exception(boost::str(boost::format("Failed to lookup policy ( OrgID/PolicyID: %1%/%2% )") % orgID % policy_id_));
										}
									}
								}

								// make sure we have DataFormat in headers
								{
									types::metadata::const_iterator itr = metadata_->find("DataFormat");
									if(itr == metadata_->end())
										throw rml::exception("No DataFormat given");

									data_type_ = types::data_sub_format::from_string(itr->second);
								}

								// open the file
								ofs_.reset(new rml::utils::tmp_fstream(creation_date_yyyymmdd_+"/"+resource_id_+".put.", content_length, confMap_["archived.cache_dir"].as<std::string>()));

								uuid_ = uuid;
								orgID_ = orgID;
								file_type_ = file_type;
								content_length_ = content_length;
								xfrd_content_length_ = 0;
								xfrd_ok_ = false;
								current_state_ = states::put_received_ack_ok;
								ostr << "OK PUT "<<file_type<<' '<<orgID<<' '<<uuid<<"\r\n";
							}
							catch(const rml::exception& e) {
								LogError(remote_ep_<<" "<<resource_id_<<": states::put_received: rml::exception: "<<e.what());
								current_state_ = states::put_received_ack_err;
								ostr << "ERR PUT "<<file_type<<' '<<orgID<<' '<<uuid<<" "<<e.what()<<"\r\n";
							}
							catch(const std::exception& e) {
								LogError(remote_ep_<<" "<<resource_id_<<": states::put_received: std::exception: "<<e.what());
								current_state_ = states::put_received_ack_err;
								ostr << "ERR PUT "<<file_type<<' '<<orgID<<' '<<uuid<<" "<<e.what()<<"\r\n";
							}
						}
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

			recv_buffer_.prepare(8192);
			boost::asio::async_read(socket_, recv_buffer_,
					boost::asio::transfer_at_least(1),
					boost::bind(&put_session::put_receive_file, shared_from_this(),
							boost::asio::placeholders::error,
							boost::asio::placeholders::bytes_transferred));

			break;
		}

		case states::put_receiving_file:
			LogError(remote_ep_<<" "<<resource_id_<<": Invalid/Unexpected state states::put_receiving_file");
		case states::put_receiving_file_err:
		{
			std::ostream ostr(&send_buffer_);
			LogError(remote_ep_<<" "<<resource_id_<<": PUT failed; general failure during transfer");
			ostr << "ERR RECV "<<file_type_ << ' ' << orgID_ << ' ' << uuid_<<" general failure during transfer\r\n";
			current_state_ = states::put_check_received_file_ack_err;

			ofs_.reset();
			md5_.assign((byte)0);
			resource_id_.clear();
			uuid_.clear();
			orgID_ = 0;
			metadata_.reset();
			data_type_ = types::data_sub_format::unknown;
			file_type_ = archiving::archivable::file_type::unknown;
			content_length_ = 0;
			policy_id_ = 0;
			creation_time_ = 0;
			xfrd_content_length_ = 0;
			xfrd_ok_ = false;

			boost::asio::async_write(socket_, send_buffer_,
					boost::bind(&put_session::run_statemachine, shared_from_this(),
							boost::asio::placeholders::error));
			break;
		}

		case states::put_receiving_file_ok:
		{
			std::ostream ostr(&send_buffer_);
			try {
				if(!xfrd_ok_) {
					LogError(remote_ep_<<" "<<resource_id_<<": general failure during transfer");
					throw protocol_exception("general failure during transfer");
				}

				ofs_->re_open();
				const archiving::hash_vector& md5 = archiving::md5hasher::hash(*ofs_);
				LogDebug(remote_ep_<<' '<<resource_id_<<" Original MD5: "<<md5_);
				LogDebug(remote_ep_<<' '<<resource_id_<<" Computed MD5: "<<md5);

				app::policy::pointer p_policy;

				if( policy_id_ != policy::pseudo_id::do_not_archive ) {
					p_policy = cached_policy_finder_(std::make_pair(orgID_, policy_id_));
					if(!p_policy) {
						cached_policy_finder_.forget(std::make_pair(orgID_, policy_id_));
						throw protocol_exception(boost::str(boost::format("Failed to lookup policy ( OrgID/PolicyID: %1%/%2% )") % orgID_ % policy_id_));
					}
				}

				if(md5 != md5_) {
					LogError(remote_ep_<<" "<<resource_id_<<": PUT failed with MD5 mismatch");
					throw protocol_exception("md5 mismatch");
				}

				LogMessage(remote_ep_<<" "<<resource_id_<<": PUT; Data received OK");

				// create operation_data
				operation_data::pointer opdata(new operation_data());
				opdata->p_detail.reset(new operation_data::detail());

				if(p_policy) {
					opdata->p_detail->policy = applied_policy(p_policy, creation_time_);
					opdata->p_detail->p_endpoint = cached_endpoint_finder_(std::make_pair<archiving::org_id_t, archiving::endpoint_id_t>(orgID_, opdata->p_detail->policy.p_policy->endpoint_id));
					opdata->p_detail->target_file_format = types::file_format::make(opdata->p_detail->p_endpoint->is_compressed, opdata->p_detail->p_endpoint->is_encrypted, data_type_);
				}

				opdata->p_detail->identifier = archiving::archivable::identifier(file_type_, orgID_, uuid_);
				opdata->p_detail->current_file_format = types::file_format::make(false, false, data_type_);
				opdata->p_detail->files_map.insert(std::make_pair(opdata->p_detail->current_file_format, ofs_));
				opdata->p_detail->clear_size = content_length_;
				opdata->p_detail->real_size = 0;
				opdata->p_detail->retry = 0;
				opdata->p_detail->database_id = 0;
				opdata->p_detail->p_metadata = metadata_;
				opdata->p_detail->state = operation_state::put_file_convert; // TODO fix this by adding a PUT / GET flag into operation data and make it work with bit-set enums
				opdata->p_detail->arrival_time = time(0);

				ostr << "OK RECV "<<file_type_<<' '<<orgID_<<' '<<uuid_<<"\r\n";

				// Looks like we have the file intact; lets make it temporarily not temporary
				ofs_->set_persistent();

				// lets add the file to clear cache for the sake of optimization
				// otherwise we will have to pull it back from storage
				clear_cache_.put(opdata->p_detail->identifier, opdata->p_detail->current_file_format, ofs_);

				current_state_ = states::put_check_received_file_ack_ok;

				// save the state of operation data
				internal_db_.save(opdata);

				// Put CDRs and those recordings needed for CDR processing on to GlusterFS
				// The decision is made in CollectD and is represented as AddToCDRMunchCache header set to "true"
				{
					std::string pbx_hostname;
					{
						types::metadata::const_iterator itr = opdata->p_detail->p_metadata->find("PBXHostName");
						if(itr == opdata->p_detail->p_metadata->end()) {
							LogWarning(remote_ep_<<" No PBXHostName found in metadata; substituting with 'dft-rm-pbx'");
							pbx_hostname = "dft-rm-pbx";
						}
						else
							pbx_hostname = itr->second;
					}

					// TODO Add AddToCDRMunchCache header in CollectD based on CDRMunch-compatibility rules
					types::metadata::const_iterator itr = opdata->p_detail->p_metadata->find("AddToCDRMunchCache");
					if(itr != opdata->p_detail->p_metadata->end()) {

						if( itr->second == "true" ) {
							LogVerbose(remote_ep_<<' '<<resource_id_<<" AddToCDRMunchCache requested");
							std::string whisper_path = boost::str(
									boost::format("%1%/%2%/%3%_%4%_%5%")
											% confMap_["distiller_client.base_dir"].as<std::string>()
											% creation_date_yyyymmdd_
											% archiving::archivable::file_type::to_string( opdata->p_detail->identifier.filetype )
											% opdata->p_detail->identifier.org
											% opdata->p_detail->identifier.uuid);

							rml::utils::directory_ready(whisper_path);
							std::ofstream whisper_ofs(whisper_path.c_str(), std::ios_base::out | std::ios_base::binary);
							if(whisper_ofs) {
								whisper_ofs.exceptions(std::ios::badbit | std::ios::failbit);

								ofs_->re_open();
								std::copy(std::istream_iterator<unsigned char>(*ofs_), std::istream_iterator<unsigned char>(), std::ostream_iterator<unsigned char>(whisper_ofs, ""));
								whisper_ofs.flush();
								whisper_ofs.close();

								LogMessage(remote_ep_<<" "<<resource_id_<<" Whisper file created; file:"<<whisper_path);

								if(file_type_ == archiving::archivable::file_type::cdr) {
									whisper::pointer p_whisper(new whisper(whisper_path, pbx_hostname, opdata->p_detail->identifier.uuid, creation_time_));
									whisper_channel_.produce(p_whisper);

									// TODO XXX handle failure to insert into cdrmunch DB
									if(!cdrmunch_db_.save(p_whisper))
										LogWarning(remote_ep_<<" Failed to add CDR into distiller queue (table: CDR.cdr_queue)");
								}
							}
							else
								LogError(remote_ep_<<" Failed to create whisper! error:"<<strerror(errno));
						}

						// we do not need to store the AddToCDRMunchCache header
						// opdata->p_detail->p_metadata->erase("AddToCDRMunchCache");
					}
				}

				// no need to keep the file open till we can process it
				ofs_->close();

				if(p_policy) // if PolicyID < 0; it only needs to be local cached
					channel_.produce(opdata);
				else
					LogMessage(remote_ep_<<" "<<opdata->p_detail->identifier<<" - NOT archived; cached for CDRMunch");
			}
			catch(const protocol_exception& e) {
				LogMessage("Protocol Exception: "<<e.what());
				ostr << "ERR RECV "<<file_type_ << ' ' << orgID_ << ' ' << uuid_<<' '<<e.what()<<"\r\n";
				current_state_ = states::put_check_received_file_ack_err;
				xfrd_ok_ = false;
			}
			catch(const std::exception& e) {
				LogWarning("Exception: "<<e.what());
				ostr << "ERR RECV "<<file_type_ << ' ' << orgID_ << ' ' << uuid_<<" general failure during transfer\r\n";
				current_state_ = states::put_check_received_file_ack_err;
				xfrd_ok_ = false;
			}
			catch(...) {
				LogWarning("Unexpected exception");
				ostr << "ERR RECV "<<file_type_ << ' ' << orgID_ << ' ' << uuid_<<" general failure during transfer\r\n";
				current_state_ = states::put_check_received_file_ack_err;
				xfrd_ok_ = false;
			}

			ofs_.reset();
			md5_.assign((byte)0);
			resource_id_.clear();
			uuid_.clear();
			orgID_ = 0;
			metadata_.reset();
			data_type_ = types::data_sub_format::unknown;
			file_type_ = archiving::archivable::file_type::unknown;
			content_length_ = 0;
			policy_id_ = 0;
			creation_time_ = 0;
			xfrd_content_length_ = 0;
			xfrd_ok_ = false;

			boost::asio::async_write(socket_, send_buffer_,
					boost::bind(&put_session::run_statemachine, shared_from_this(),
							boost::asio::placeholders::error));
			break;
		}

		}
	}

	void put_receive_file(const boost::system::error_code& error, std::size_t bytes_transferred)
	{
		const boost::asio::ip::tcp::socket::endpoint_type& remote_ep_( socket_.remote_endpoint() );

		/*
		LogDebug(remote_ep_<<' '<<resource_id_<<" error: "<<error<<" ---------------------------------------");
		LogDebug(remote_ep_<<' '<<resource_id_<<" bytes_transferred: "<<bytes_transferred);
		LogDebug(remote_ep_<<' '<<resource_id_<<" content_length_: "<<content_length_);
		 */

		std::size_t chunk_remaining = bytes_transferred;

		if(!error) {
			while(chunk_remaining) {

				std::size_t buf_length = std::min<std::size_t>(bytes_transferred, content_length_ - xfrd_content_length_);
				if(buf_length) {
					std::vector<char> buf(buf_length);
					buf_length = recv_buffer_.sgetn(&buf[0], buf_length);
					ofs_->rdbuf()->sputn(&buf[0], buf_length);
					xfrd_content_length_ += buf_length;
					chunk_remaining -= buf_length;

					LogDebug(remote_ep_<<' '<<resource_id_<<" xfrd_content_length_: "<<xfrd_content_length_<<" / "<<content_length_);
				}
				else {
					if ((chunk_remaining == 1) && (content_length_ == xfrd_content_length_)) {

						char eof[4];
						std::streamsize eof_len = recv_buffer_.sgetn(eof, 4);

						//LogDebug(remote_ep_<<' '<<resource_id_<<" eof_len: "<<eof_len);
						//LogDebug(remote_ep_<<' '<<resource_id_<<" eof: x -"<<eof[0]<<"- x ("<<int(eof[0])<<")");

						if((1 == eof_len) && (eof[0] == '\x1a')) {
							xfrd_ok_ = true;
							chunk_remaining = 0;
							LogMessage(remote_ep_<<' '<<resource_id_<<" PUT; File transfer completed ok");
						}
						else {
							LogError(remote_ep_<<' '<<resource_id_<<" Unexpected data; eof expected, found '"<<RML_HEX(4, int(eof[0]))<<RML_HEX_RESET<<"'");
							break;
						}
					}
					else {
						LogDebug(remote_ep_<<' '<<resource_id_<<" chunk_remaining:"<<chunk_remaining);
						LogDebug(remote_ep_<<' '<<resource_id_<<" content_length_:"<<content_length_);
						LogDebug(remote_ep_<<' '<<resource_id_<<" xfrd_content_length_:"<<xfrd_content_length_);
						LogError(remote_ep_<<' '<<resource_id_<<" Unexpected data; NOT ((chunk_remaining == 1) && (content_length_ == xfrd_content_length_))");
						break;
					}
				}
			}

			if((xfrd_content_length_ == content_length_) && xfrd_ok_) {
				current_state_ = states::put_receiving_file_ok;
				io_service_.post(boost::bind(&put_session::run_statemachine, shared_from_this(), error));
			}
			else if(chunk_remaining != 0) {
				LogError(remote_ep_<<" Invalid state; chunk_remaining != 0");
				current_state_ = states::put_receiving_file_err;
				io_service_.post(boost::bind(&put_session::run_statemachine, shared_from_this(), error));
			}
			else {
				recv_buffer_.prepare(8192);
				boost::asio::async_read(socket_, recv_buffer_,
						boost::asio::transfer_at_least(1),
						boost::bind(&put_session::put_receive_file, shared_from_this(),
								boost::asio::placeholders::error,
								boost::asio::placeholders::bytes_transferred));
			}
		}
		else {
			LogError(remote_ep_<<" Error while receiving PUT data: "<<error.message());
			current_state_ = states::put_receiving_file_err;
			io_service_.post(boost::bind(&put_session::run_statemachine, shared_from_this(), error));
		}
	}

	void do_close()
	{
		current_state_ = states::closed;
		socket_.close();
	}
};

}  // namespace app

#endif /* COLLECTD_SESSION_HPP_ */
