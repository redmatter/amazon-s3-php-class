/*
 * storage_backend.hpp
 *
 *  Created on: 6 Feb 2011
 *      Author: dkorah
 */

#ifndef BASIC_STORAGE_BACKEND_HPP_
#define BASIC_STORAGE_BACKEND_HPP_

#include <stdint.h>

#include <fstream>
#include <string>

#include <boost/noncopyable.hpp>

#include <libaws/aws.h>

#include <rml/exception.h>

#include "types.hpp"
#include "storage_operation.hpp"
#include "storage_object_type.hpp"
#include "storage_endpoint.hpp"

namespace app {

namespace storage {


template < typename Backend, typename object_identifier_t >
class basic_backend : public boost::noncopyable
{
	static Backend* p_backend_;

public:
	static void destroy()
	{
		static bool destroyed = false;
		if(!destroyed) {
			delete p_backend_;
			p_backend_ = 0;
		}
	}

	static Backend* get_instance(const boost::program_options::variables_map& confMap = boost::program_options::variables_map())
	{
		if(!p_backend_) {
			assert(!confMap.empty());
			p_backend_ = new Backend(confMap);
			std::atexit(destroy);
		}

		return p_backend_;
	}

	virtual	boost::shared_ptr<std::fstream> open(object_identifier_t&, const operations::operations_t&, const std::iostream::openmode&) = 0;

	virtual ~basic_backend()
	{
		//virtual do nothing!
	};

};

template <typename Backend, typename object_identifier_t>
Backend* basic_backend< Backend, object_identifier_t >::p_backend_(0);

// specialisation

namespace s3 {

struct object_identifier
{
	/// the bucket description
	endpoint::pointer p_endpoint;
	/// the object key
	std::string identifier;
};

namespace specific {

struct cached_endpoint_pointer_compare_lt
{
	bool operator()(const endpoint::pointer& lhs, const endpoint::pointer& rhs) {
		if(lhs->access_key != rhs->access_key)
			return lhs->access_key < rhs->access_key;

		return lhs->secret_key < rhs->secret_key;
	}
};

typedef boost::shared_ptr< aws::ConnectionPool< aws::S3ConnectionPtr > > connection_pool_ptr;

class connection_pool_finder
{
	const boost::program_options::variables_map& confMap_;

public:
	connection_pool_finder(const boost::program_options::variables_map& confMap) : confMap_(confMap)
	{
		//ctor
	}

	connection_pool_ptr operator()(const endpoint::pointer& ep) const
	{
		return connection_pool_ptr( new aws::ConnectionPool<aws::S3ConnectionPtr>(
				confMap_["s3_backend.pool_size"].as<unsigned int>(),
				ep->access_key, ep->secret_key));
	}
};

typedef cache::result_cache<const endpoint::pointer&, connection_pool_ptr, cached_endpoint_pointer_compare_lt> cached_connection_pool_finder_t;

}  // namespace specific


class backend : public basic_backend<backend, object_identifier>
{
	const boost::program_options::variables_map& confMap_;
	specific::connection_pool_finder connection_pool_finder_;
	specific::cached_connection_pool_finder_t cached_connection_pool_finder_;

protected:
	backend(const boost::program_options::variables_map& confMap) :
		confMap_(confMap),
		connection_pool_finder_(confMap_),
		cached_connection_pool_finder_(connection_pool_finder_)
	{
		LogMessage("S3 backend created");
	}

	virtual ~backend()
	{
		LogMessage("S3 backend destroyed");
	}

public:

	friend class basic_backend<backend, object_identifier>;

	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("s3_backend.pool_size",  boost::program_options::value<unsigned int>()->default_value(5), "S3 connection pool size")
				;
	}

	void get(operation_data::pointer opdata)
	{
		if(opdata->state != operation_states::get_file_retrieve)
			THROW_RML_EXCEPTION(opdata->p_detail->p_endpoint->bucket<<'/'<<opdata->p_detail->identifier<<": invalid state");

		try {
			aws::S3ConnectionPtr p_s3Conn = cached_connection_pool_finder_(opdata->p_detail->p_endpoint)->getConnection();

			types::file_format::file_format_t s3_fileformat;
			types::file_format::file_format_t target_fileformat;
			{
				aws::HeadResponsePtr p_headRes = p_s3Conn->head(opdata->p_detail->p_endpoint->bucket, opdata->p_detail->identifier);
				const std::map<std::string, std::string>& metadata(p_headRes->getMetaData());
				std::map<std::string, std::string>::const_iterator itr_is_compressed = metadata.find("FileTypeIsCompressed");
				std::map<std::string, std::string>::const_iterator itr_is_encrypted  = metadata.find("FileTypeIsEncrypted");
				std::map<std::string, std::string>::const_iterator itr_filetype  = metadata.find("FileType");
				std::map<std::string, std::string>::const_iterator itr_end = metadata.end();
				types::data_sub_format::data_sub_format_t dsf = types::data_sub_format::binary;
				if(itr_filetype != itr_end) {
					dsf = types::data_sub_format::from_string(itr_filetype->second);
					if(dsf == types::data_sub_format::unknown) {
						LogError(opdata->p_detail->p_endpoint->bucket<<'/'<<opdata->p_detail->identifier<<": invalid header rm-file-type: "<<itr_filetype->second<<"; using 'binary' instead");
						dsf = types::data_sub_format::binary;
					}
				}
				else
					LogError(opdata->p_detail->p_endpoint->bucket<<'/'<<opdata->p_detail->identifier<<": NO header rm-file-type; using 'binary'");

				s3_fileformat = types::file_format::make(
						((itr_is_compressed != itr_end) && (itr_is_compressed->second == "true")),
						((itr_is_encrypted != itr_end) && (itr_is_encrypted->second == "true")),
						dsf);

				target_fileformat = types::file_format::make(false, false, dsf);
			}

			rml::utils::tmp_fstream::pointer fstr = opdata->get_tmp_fstream(s3_fileformat, true, true); // create & force new
			{
				aws::GetResponsePtr p_getRes = p_s3Conn->get(opdata->p_detail->p_endpoint->bucket, opdata->p_detail->identifier);
				*fstr << p_getRes->getInputStream().rdbuf();
			}

			opdata->p_detail->current_file_format = s3_fileformat;
			opdata->p_detail->target_file_format = target_fileformat;
		}
		catch(const aws::HeadException& e) {
			THROW_RML_EXCEPTION(opdata->p_detail->p_endpoint->bucket<<'/'<<opdata->p_detail->identifier<<" - GET failed - (HEAD) error:"<<e.what());
		}
		catch(const aws::GetException& e) {
			THROW_RML_EXCEPTION(opdata->p_detail->p_endpoint->bucket<<'/'<<opdata->p_detail->identifier<<" - GET failed - (GET) error:"<<e.what());
		}
	}

	void put(operation_data::pointer opdata)
	{
		if(opdata->state != operation_states::put_file_save && opdata->p_detail->retry > 0)
			THROW_RML_EXCEPTION(opdata->p_detail->p_endpoint->bucket<<'/'<<opdata->p_detail->identifier<<": invalid state (retry: "<<opdata->p_detail->retry<<')');

		try {
			aws::S3ConnectionPtr p_s3Conn = cached_connection_pool_finder_(opdata->p_detail->p_endpoint)->getConnection();

			rml::utils::tmp_fstream::pointer tmpfs = opdata->get_tmp_fstream(opdata->p_detail->target_file_format, false, false);
			if(!tmpfs)
				THROW_RML_EXCEPTION(opdata->p_detail->p_endpoint->bucket<<'/'<<opdata->p_detail->identifier<<": No target format found in opdata");

			assert(false == opdata->p_detail->p_metadata->empty());
			std::map< std::string, std::string > headers;
			{
				opdata->p_detail->p_metadata->make_headers(headers);
				headers.insert(std::make_pair(
						"FileTypeIsCompressed",
						types::file_format::is_compressed(opdata->p_detail->target_file_format)? "true": "false"));
				headers.insert(std::make_pair(
						"FileTypeIsEncrypted",
						types::file_format::is_encrypted(opdata->p_detail->target_file_format)? "true": "false"));
				headers.insert(std::make_pair(
						"FileType",
						types::data_sub_format::to_string(
								types::data_sub_format::data_sub_format_t(
										opdata->p_detail->target_file_format & 0xff))));
			}

			aws::PutResponsePtr p_putRes = p_s3Conn->put(opdata->p_detail->p_endpoint->bucket, opdata->p_detail->identifier,
					*reinterpret_cast<std::ifstream*>(tmpfs.get()), "application/octet-stream", &headers, -1);
		}
		catch(const aws::PutException& e) {
			THROW_RML_EXCEPTION(opdata->p_detail->p_endpoint->bucket<<'/'<<opdata->p_detail->identifier<<" - PUT failed - error:"<<e.what());
		}
	}
};

}  // namespace s3

}  // namespace storage

}  // namespace app

#endif /* BASIC_STORAGE_BACKEND_HPP_ */
