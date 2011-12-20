/*
 * storage_backend.hpp
 *
 *  Created on: 6 Feb 2011
 *      Author: dkorah
 */

#ifndef BASIC_STORAGE_BACKEND_HPP_
#define BASIC_STORAGE_BACKEND_HPP_

#include "types.hpp"
#include "storage_operation.hpp"
#include "storage_endpoint.hpp"

#include <rml/exception.h>

#include <libs3.h>

#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#include <cryptopp/cryptlib.h>
#include <cryptopp/filters.h>
#include <cryptopp/modes.h>
#include <cryptopp/files.h>
#include <cryptopp/md5.h>
#include <cryptopp/base64.h>
#undef CRYPTOPP_ENABLE_NAMESPACE_WEAK

#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>

#include <fstream>
#include <string>
#include <map>

#include <stdint.h>
#include <strings.h>

namespace {

struct ci_char_traits: public std::char_traits<char> {

	static bool eq(char c1, char c2) {
		return toupper(c1) == toupper(c2);
	}

	static bool ne(char c1, char c2) {
		return toupper(c1) != toupper(c2);
	}

	static bool lt(char c1, char c2) {
		return toupper(c1) < toupper(c2);
	}

	static int compare(const char* s1, const char* s2, size_t n) {
		while (n-- != 0) {
			if (toupper(*s1) < toupper(*s2))
				return -1;
			if (toupper(*s1) > toupper(*s2))
				return 1;
			++s1;
			++s2;
		}
		return 0;
	}

	static const char* find(const char* s, int n, char a) {
		while (n-- > 0 && toupper(*s) != toupper(a)) {
			++s;
		}
		return s;
	}

};

typedef std::basic_string<char, ci_char_traits> ci_string;

}

extern "C" {
S3Status get_response_properties_cb_(const S3ResponseProperties *properties, void *callbackData);
S3Status head_response_properties_cb_(const S3ResponseProperties *properties, void *callbackData);
S3Status put_response_properties_cb_(const S3ResponseProperties *properties, void *callbackData);
void get_response_complete_cb_(S3Status status, const S3ErrorDetails *error, void *callbackData);
void head_response_complete_cb_(S3Status status, const S3ErrorDetails *error, void *callbackData);
void put_response_complete_cb_(S3Status status, const S3ErrorDetails *error, void *callbackData);
int put_object_data_cb_(int bufferSize, char *buffer, void *callbackData);
S3Status get_object_data_cb_(int bufferSize, const char *buffer, void *callbackData);
}

namespace app {

namespace storage {

typedef std::map< std::string, std::string > string_map_t;

template < typename Backend >
class basic_backend : public boost::noncopyable
{
public:
	typedef boost::shared_ptr< basic_backend > pointer;

	static pointer get_instance(const boost::program_options::variables_map& confMap = boost::program_options::variables_map())
	{
		static pointer p_backend_( new Backend(confMap) );
		return p_backend_;
	}

	virtual ~basic_backend()
	{
		//virtual do nothing!
	};

	virtual void put(operation_data::pointer) = 0;
	virtual void get(operation_data::pointer) = 0;
	virtual void stat(operation_data::pointer, string_map_t&) = 0;
	virtual void remove(operation_data::pointer) = 0;
};

// specialisation

namespace s3 {

class backend : public basic_backend< backend >
{
	const boost::program_options::variables_map& confMap_;

	friend S3Status ::get_response_properties_cb_(const S3ResponseProperties*, void*);
	friend S3Status ::head_response_properties_cb_(const S3ResponseProperties*, void*);
	friend S3Status ::put_response_properties_cb_(const S3ResponseProperties*, void*);
	friend void ::get_response_complete_cb_(S3Status, const S3ErrorDetails*, void*);
	friend void ::head_response_complete_cb_(S3Status, const S3ErrorDetails*, void*);
	friend void ::put_response_complete_cb_(S3Status, const S3ErrorDetails*, void*);
	friend int ::put_object_data_cb_(int, char*, void*);
	friend S3Status ::get_object_data_cb_(int, const char*, void*);

protected:
	backend(const boost::program_options::variables_map& confMap) :
		confMap_(confMap)
	{
		LogMessage("S3 backend created");
	}

	struct put_callback_data
	{
		rml::utils::tmp_fstream::pointer p_tmpfs_;
		std::size_t to_be_copied;
		S3Status status;
		std::string base64_md5;

		put_callback_data() : p_tmpfs_(), to_be_copied(0), status(S3StatusInternalError)
		{
		}

		put_callback_data(rml::utils::tmp_fstream::pointer p_tmpfs) :
			p_tmpfs_(p_tmpfs),
			to_be_copied(p_tmpfs->get_size()),
			status(S3StatusInternalError)
		{
			p_tmpfs_->re_open();

			CryptoPP::Weak1::MD5 hash;
			CryptoPP::FileSource f(*p_tmpfs_, true,
					new CryptoPP::HashFilter(hash,
							new CryptoPP::Base64Encoder(
									new CryptoPP::StringSink(base64_md5), false)));

			if(!(*p_tmpfs_) && !p_tmpfs_->eof())
				throw rml::exception("Error while creating md5");

			LogDebug("Base64(MD5): "<<base64_md5);

			p_tmpfs_->clear(); // clear eof
			p_tmpfs_->seekg(0);
		}
		
		~put_callback_data()
		{
			if(p_tmpfs_)
				p_tmpfs_->close();
		}
	};
	
	struct get_callback_data
	{
		rml::utils::tmp_fstream::pointer p_tmpfs_;
		S3Status status;
		std::string md5; // TODO check MD5 after download

		get_callback_data() : p_tmpfs_()
		{
		}

		get_callback_data(rml::utils::tmp_fstream::pointer p_tmpfs) : p_tmpfs_(p_tmpfs), status(S3StatusInternalError)
		{
		}
		
		~get_callback_data()
		{
			if(p_tmpfs_)
				p_tmpfs_->close();
		}
	};
	
	struct head_callback_data
	{
		S3Status status;
		string_map_t& headers;
		
		head_callback_data(string_map_t& headers) : status(S3StatusInternalError), headers(headers)
		{
		}
	};

	static S3Status put_response_properties_cb(const S3ResponseProperties *properties, void *callbackData)
	{
		return S3StatusOK;
	}

	static S3Status head_response_properties_cb(const S3ResponseProperties *properties, void *callbackData)
	{
		head_callback_data *data = (head_callback_data *) callbackData;
		
		if(properties->requestId)
			data->headers["request-id"] = properties->requestId;
		
		if(properties->requestId2)
			data->headers["request-id2"] = properties->requestId2;
		
		if(properties->contentLength) {
			std::ostringstream ostr;
			ostr << properties->contentLength;
			
			data->headers["content-length"] = ostr.str();
		}

		if(properties->contentType)
			data->headers["content-type"] = properties->contentType;

		if(properties->server)
			data->headers["server"] = properties->server;
		
		if(properties->eTag)
			data->headers["etag"] = properties->eTag;
		
		if(properties->lastModified) {
			std::ostringstream ostr;
			ostr << properties->lastModified;
			
			data->headers["last-modified"] = ostr.str();
		}
		
		if(properties->metaDataCount) {
			for(int i = 0; i < properties->metaDataCount; ++i)
				data->headers[properties->metaData[i].name] = properties->metaData[i].value;
		}

		return S3StatusOK;
	}

	static S3Status get_response_properties_cb(const S3ResponseProperties *properties, void *callbackData)
	{
		return S3StatusOK;
	}

	static void put_response_complete_cb(S3Status status, const S3ErrorDetails *error, void *callbackData)
	{
		put_callback_data *data = (put_callback_data *) callbackData;
		data->status = status;

		LogDebug("status: "<<S3_get_status_name(status));

		if (error && error->message)
			LogError("Error: "<<error->message);

		if (error && error->resource)
			LogError("Resource: "<<error->resource);

		if (error && error->furtherDetails)
			LogError("Further Details: "<<error->furtherDetails);

		if (error && error->extraDetailsCount) {
			LogError("Extra Details:");
			int i;
			for (i = 0; i < error->extraDetailsCount; i++)
				LogError(error->extraDetails[i].name << ": " << error->extraDetails[i].value);
		}
	}

	static void head_response_complete_cb(S3Status status, const S3ErrorDetails *error, void *callbackData)
	{
		head_callback_data *data = (head_callback_data *) callbackData;
		data->status = status;

		LogDebug("status: "<<S3_get_status_name(status));

		if (error && error->message)
			LogError("Error: "<<error->message);

		if (error && error->resource)
			LogError("Resource: "<<error->resource);

		if (error && error->furtherDetails)
			LogError("Further Details: "<<error->furtherDetails);

		if (error && error->extraDetailsCount) {
			LogError("Extra Details:");
			int i;
			for (i = 0; i < error->extraDetailsCount; i++)
				LogError(error->extraDetails[i].name << ": " << error->extraDetails[i].value);
		}
	}

	static void get_response_complete_cb(S3Status status, const S3ErrorDetails *error, void *callbackData)
	{
		get_callback_data *data = (get_callback_data *) callbackData;
		data->status = status;

		LogDebug("status: "<<S3_get_status_name(status));

		if (error && error->message)
			LogError("Error: "<<error->message);

		if (error && error->resource)
			LogError("Resource: "<<error->resource);

		if (error && error->furtherDetails)
			LogError("Further Details: "<<error->furtherDetails);

		if (error && error->extraDetailsCount) {
			LogError("Extra Details:");
			int i;
			for (i = 0; i < error->extraDetailsCount; i++)
				LogError(error->extraDetails[i].name << ": " << error->extraDetails[i].value);
		}
	}

	static int put_object_data_cb(int bufferSize, char *buffer, void *callbackData)
	{
		put_callback_data *data = (put_callback_data *) callbackData;

		int ret = 0;

		if (data->to_be_copied) {
			int read_size = std::min<int>(bufferSize, data->to_be_copied);
			data->p_tmpfs_->read(buffer, read_size);
			ret = data->p_tmpfs_->gcount();
		}

		data->to_be_copied -= ret;
		return ret;
	}


	static S3Status get_object_data_cb(int bufferSize, const char *buffer, void *callbackData)
	{
		get_callback_data *data = (get_callback_data *) callbackData;

		S3Status ret = S3StatusAbortedByCallback;
		
		if(data->p_tmpfs_->write(buffer, bufferSize))
			ret = S3StatusOK;
		else
			LogWarning("Failed writing to file "<<data->p_tmpfs_->get_filename());

		return ret;
	}
	
public:

	friend class basic_backend< backend >;

	virtual ~backend()
	{
		LogMessage("S3 backend destroyed");
	}

	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("s3_backend.ssl_enabled",  boost::program_options::value<std::string>()->default_value("true"), "Use SSL for S3 connection")
				;
	}

	void get(operation_data::pointer opdata)
	{
		if(opdata->p_detail->state != operation_state::get_file_retrieve && opdata->p_detail->retry > 0)
			THROW_RML_EXCEPTION(opdata->p_detail->identifier<<": invalid state (retry: "<<opdata->p_detail->retry<<')');
		
		rml::utils::tmp_fstream::pointer ofs = opdata->get_tmp_fstream(opdata->p_detail->current_file_format, true, true);
		types::caseless_key_string_map headers;
		
		get(opdata->p_detail->p_endpoint, std::string(opdata->p_detail->identifier), ofs, headers);
	}
	
	inline void get(
			const basic_endpoint::pointer p_endpoint,
			const std::string& identifier,
			rml::utils::tmp_fstream::pointer ofs,
			types::caseless_key_string_map& headers)
	{
		S3BucketContext bucket_context;
		
		endpoint::pointer s3_endpoint = boost::static_pointer_cast<endpoint>(p_endpoint);
		bucket_context.bucketName = &s3_endpoint->bucket[0];
		bucket_context.accessKeyId = &s3_endpoint->access_key[0];
		bucket_context.secretAccessKey = &s3_endpoint->secret_key[0];
		if(p_endpoint->type == endpoint_type::s3_compatible) {
			s3_compatible::endpoint::pointer s3c_endpoint = boost::static_pointer_cast<s3_compatible::endpoint>(p_endpoint);
			bucket_context.protocol = S3ProtocolHTTP;
			bucket_context.uriStyle = S3UriStylePath;
			bucket_context.baseHostname = &s3c_endpoint->host[0];
			bucket_context.serviceURI = &s3c_endpoint->service_uri[0];
		}
		else {
			bucket_context.protocol = ( (confMap_["s3_backend.ssl_enabled"].as<std::string>() == "true")? S3ProtocolHTTPS : S3ProtocolHTTP );
			bucket_context.uriStyle = S3UriStyleVirtualHost;
			bucket_context.baseHostname = 0;
			bucket_context.serviceURI = 0;
		}

	    S3GetObjectHandler get_handler;
	    get_handler.responseHandler.completeCallback = &get_response_complete_cb_;
	    get_handler.responseHandler.propertiesCallback = &get_response_properties_cb_;
	    get_handler.getObjectDataCallback = &get_object_data_cb_;
	    
	    get_callback_data data(ofs);

		int retry = 2;
	    do {
	        S3_get_object(&bucket_context, &identifier[0], 0, 0, 0, 0, &get_handler, &data);
	    } while (data.status != S3StatusOK && S3_status_is_retryable(data.status) && --retry);

		LogMessage(identifier<<": GET Result Status: "<<S3_get_status_name(data.status));

		if(data.status != S3StatusOK)
			throw rml::exception("GET failed");
	}

	void put(operation_data::pointer opdata)
	{
		if(opdata->p_detail->state != operation_state::put_file_save && opdata->p_detail->retry > 0)
			THROW_RML_EXCEPTION(opdata->p_detail->identifier<<": invalid state (retry: "<<opdata->p_detail->retry<<')');

		rml::utils::tmp_fstream::pointer tmpfs = opdata->get_tmp_fstream(opdata->p_detail->target_file_format, false, false);
		if(!tmpfs)
			THROW_RML_EXCEPTION(opdata->p_detail->identifier<<": No target format found in opdata");

		assert(false == opdata->p_detail->p_metadata->empty());
		types::caseless_key_string_map headers;
		opdata->p_detail->p_metadata->make_headers(headers);

		put(opdata->p_detail->p_endpoint, std::string(opdata->p_detail->identifier), tmpfs, opdata->p_detail->target_file_format, headers);
	}

	inline void put(
			basic_endpoint::pointer p_endpoint,
			const std::string& identifier,
			rml::utils::tmp_fstream::pointer tmpfs,
			const types::file_format::file_format_t& target_file_format,
			const types::caseless_key_string_map& headers = types::caseless_key_string_map())
	{
		types::caseless_key_string_map s3headers = headers;
		{
			s3headers.insert(std::make_pair(
					"file-type-is-compressed",
					types::file_format::is_compressed(target_file_format)? "true": "false"));
			s3headers.insert(std::make_pair(
					"file-type-is-encrypted",
					types::file_format::is_encrypted(target_file_format)? "true": "false"));
			s3headers.insert(std::make_pair(
					"file-type",
					types::data_sub_format::to_string(types::data_sub_format::data_sub_format_t(target_file_format & 0xff))));
		}


		S3NameValue metadata[s3headers.size()];
		LogDebug(identifier<<": Adding "<<s3headers.size()<<" metadata headers");
		int i = 0;
		for(types::caseless_key_string_map::const_iterator itr = s3headers.begin(); itr != s3headers.end(); ++itr) {
			if(!itr->first.empty() && !itr->second.empty()) {
				metadata[i].name = &(*itr).first[0];
				metadata[i].value = &(*itr).second[0];

				LogVerbose(identifier<<": Metadata["<<i<<"] - name: "<<metadata[i].name<<" value: "<< metadata[i].value);

				i++;
			}
		}

		put_callback_data data(tmpfs);

		S3PutProperties put_props;
		put_props.contentType = 0;
		put_props.md5 = &data.base64_md5[0];
		put_props.metaDataCount = s3headers.size();
		put_props.metaData = metadata;
		put_props.cacheControl = 0;
		put_props.contentDispositionFilename = 0;
		put_props.contentEncoding = 0;
		put_props.expires = -1;
		put_props.cannedAcl = S3CannedAclPrivate;

		S3BucketContext bucket_context;
		
		endpoint::pointer s3_endpoint = boost::static_pointer_cast<endpoint>(p_endpoint);
		bucket_context.bucketName = &s3_endpoint->bucket[0];
		bucket_context.accessKeyId = &s3_endpoint->access_key[0];
		bucket_context.secretAccessKey = &s3_endpoint->secret_key[0];
		if(p_endpoint->type == endpoint_type::s3_compatible) {
			s3_compatible::endpoint::pointer s3c_endpoint = boost::static_pointer_cast<s3_compatible::endpoint>(p_endpoint);
			bucket_context.protocol = S3ProtocolHTTP;
			bucket_context.uriStyle = S3UriStylePath;
			bucket_context.baseHostname = &s3c_endpoint->host[0];
			bucket_context.serviceURI = &s3c_endpoint->service_uri[0];
		}
		else {
			bucket_context.protocol = ( (confMap_["s3_backend.ssl_enabled"].as<std::string>() == "true")? S3ProtocolHTTPS : S3ProtocolHTTP );
			bucket_context.uriStyle = S3UriStyleVirtualHost;
			bucket_context.baseHostname = 0;
			bucket_context.serviceURI = 0;
		}
		
		S3PutObjectHandler put_handler;
		put_handler.responseHandler.completeCallback = &put_response_complete_cb_;
		put_handler.responseHandler.propertiesCallback = &put_response_properties_cb_;
		put_handler.putObjectDataCallback = &put_object_data_cb_;

		int retry = 2;
		do {
			S3_put_object(&bucket_context, &identifier[0], data.to_be_copied, &put_props, 0, &put_handler, &data);
		} while (data.status != S3StatusOK && S3_status_is_retryable(data.status) && --retry);

		LogMessage(identifier<<": PUT Result Status: "<<S3_get_status_name(data.status));

		if(data.status != S3StatusOK)
			throw rml::exception("S3 PUT failed");
	}

	void stat(operation_data::pointer opdata, string_map_t& res)
	{
		stat(opdata->p_detail->p_endpoint, opdata->p_detail->identifier, res);
	}
	
	void stat(basic_endpoint::pointer p_endpoint, const std::string& identifier, string_map_t& res)
	{
		S3BucketContext bucket_context;
		
		endpoint::pointer s3_endpoint = boost::static_pointer_cast<endpoint>(p_endpoint);
		bucket_context.bucketName = &s3_endpoint->bucket[0];
		bucket_context.accessKeyId = &s3_endpoint->access_key[0];
		bucket_context.secretAccessKey = &s3_endpoint->secret_key[0];
		if(p_endpoint->type == endpoint_type::s3_compatible) {
			s3_compatible::endpoint::pointer s3c_endpoint = boost::static_pointer_cast<s3_compatible::endpoint>(p_endpoint);
			bucket_context.protocol = S3ProtocolHTTP;
			bucket_context.uriStyle = S3UriStylePath;
			bucket_context.baseHostname = &s3c_endpoint->host[0];
			bucket_context.serviceURI = &s3c_endpoint->service_uri[0];
		}
		else {
			bucket_context.protocol = ( (confMap_["s3_backend.ssl_enabled"].as<std::string>() == "true")? S3ProtocolHTTPS : S3ProtocolHTTP );
			bucket_context.uriStyle = S3UriStyleVirtualHost;
			bucket_context.baseHostname = 0;
			bucket_context.serviceURI = 0;
		}

		S3ResponseHandler head_handler;
		head_handler.completeCallback = &head_response_complete_cb_;
		head_handler.propertiesCallback = &head_response_properties_cb_;
	    
	    head_callback_data data(res);

		int retry = 2;
	    do {
	        S3_head_object(&bucket_context, &identifier[0], 0, &head_handler, &data);
	    } while (data.status != S3StatusOK && S3_status_is_retryable(data.status) && --retry);

		LogMessage(identifier<<": GET Result Status: "<<S3_get_status_name(data.status));

		if(data.status != S3StatusOK)
			throw rml::exception("GET failed");
	}

	void remove(operation_data::pointer opdata)
	{

	}

};

}  // namespace s3

}  // namespace storage

}  // namespace app

extern "C" {

S3Status put_response_properties_cb_(const S3ResponseProperties *properties, void *callbackData)
{
	S3Status ret = S3StatusErrorUnknown;

	try {
		ret = app::storage::s3::backend::put_response_properties_cb(properties, callbackData);
	}
	catch(const std::exception& e) {
		LogError(e.what());
	}
	catch(...) {
		LogError("Unexpected exception");
	}

	return ret;
}

S3Status head_response_properties_cb_(const S3ResponseProperties *properties, void *callbackData)
{
	S3Status ret = S3StatusErrorUnknown;

	try {
		ret = app::storage::s3::backend::head_response_properties_cb(properties, callbackData);
	}
	catch(const std::exception& e) {
		LogError(e.what());
	}
	catch(...) {
		LogError("Unexpected exception");
	}

	return ret;
}

S3Status get_response_properties_cb_(const S3ResponseProperties *properties, void *callbackData)
{
	S3Status ret = S3StatusErrorUnknown;

	try {
		ret = app::storage::s3::backend::get_response_properties_cb(properties, callbackData);
	}
	catch(const std::exception& e) {
		LogError(e.what());
	}
	catch(...) {
		LogError("Unexpected exception");
	}

	return ret;
}

void put_response_complete_cb_(S3Status status, const S3ErrorDetails *error, void *callbackData)
{
	try {
		app::storage::s3::backend::put_response_complete_cb(status, error, callbackData);
	}
	catch(const std::exception& e) {
		LogError(e.what());
	}
	catch(...) {
		LogError("Unexpected exception");
	}
}

void get_response_complete_cb_(S3Status status, const S3ErrorDetails *error, void *callbackData)
{
	try {
		app::storage::s3::backend::get_response_complete_cb(status, error, callbackData);
	}
	catch(const std::exception& e) {
		LogError(e.what());
	}
	catch(...) {
		LogError("Unexpected exception");
	}
}

void head_response_complete_cb_(S3Status status, const S3ErrorDetails *error, void *callbackData)
{
	try {
		app::storage::s3::backend::head_response_complete_cb(status, error, callbackData);
	}
	catch(const std::exception& e) {
		LogError(e.what());
	}
	catch(...) {
		LogError("Unexpected exception");
	}
}

int put_object_data_cb_(int bufferSize, char *buffer, void *callbackData)
{
	int ret = -1;

	try {
		ret = app::storage::s3::backend::put_object_data_cb(bufferSize, buffer, callbackData);
	}
	catch(const std::exception& e) {
		LogError(e.what());
	}
	catch(...) {
		LogError("Unexpected exception");
	}

	return ret;
}


S3Status get_object_data_cb_(int bufferSize, const char *buffer, void *callbackData)
{
	S3Status ret = S3StatusAbortedByCallback;

	try {
		ret = app::storage::s3::backend::get_object_data_cb(bufferSize, buffer, callbackData);
	}
	catch(const std::exception& e) {
		LogError(e.what());
	}
	catch(...) {
		LogError("Unexpected exception");
	}

	return ret;
}

}

#endif /* BASIC_STORAGE_BACKEND_HPP_ */
