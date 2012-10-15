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
#include <rml/utils.h>

#include <libs3.h>

#include <openssl/crypto.h>

#include <cryptopp/cryptlib.h>
#include <cryptopp/filters.h>
#include <cryptopp/modes.h>
#include <cryptopp/files.h>
#include <cryptopp/md5.h>
#include <cryptopp/base64.h>

#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>

#include <fstream>
#include <string>
#include <map>

#include <pthread.h>
#include <stdint.h>
//#include <strings.h>
#include <string.h>

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
S3Status multidelete_response_properties_cb_(const S3ResponseProperties *properties, void *callbackData);
S3Status head_response_properties_cb_(const S3ResponseProperties *properties, void *callbackData);
S3Status put_response_properties_cb_(const S3ResponseProperties *properties, void *callbackData);
void get_response_complete_cb_(S3Status status, const S3ErrorDetails *error, void *callbackData);
void head_response_complete_cb_(S3Status status, const S3ErrorDetails *error, void *callbackData);
void multidelete_response_complete_cb_(S3Status status, const S3ErrorDetails *error, void *callbackData);
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
	std::vector< boost::shared_ptr< boost::mutex > > openssl_mutexes_;

	friend S3Status ::get_response_properties_cb_(const S3ResponseProperties*, void*);
	friend S3Status ::multidelete_response_properties_cb_(const S3ResponseProperties *, void *);
	friend S3Status ::head_response_properties_cb_(const S3ResponseProperties*, void*);
	friend S3Status ::put_response_properties_cb_(const S3ResponseProperties*, void*);
	friend void ::get_response_complete_cb_(S3Status, const S3ErrorDetails*, void*);
	friend void ::head_response_complete_cb_(S3Status, const S3ErrorDetails*, void*);
	friend void ::multidelete_response_complete_cb_(S3Status, const S3ErrorDetails *, void *);
	friend void ::put_response_complete_cb_(S3Status, const S3ErrorDetails*, void*);
	friend int ::put_object_data_cb_(int, char*, void*);
	friend S3Status ::get_object_data_cb_(int, const char*, void*);

protected:
	backend(const boost::program_options::variables_map& confMap) :
		confMap_(confMap)
	{
		static const std::string useragent("RM-HTTPClient");
		S3Status initStatus = S3_initialize(&useragent[0], S3_INIT_ALL);
		if(S3StatusOK != initStatus)
			THROW_RML_EXCEPTION("S3 backend init failed; libs3 error:"<<S3_get_status_name(initStatus));

		openssl_mutexes_.resize(CRYPTO_num_locks());
        for(size_t i = 0; i < openssl_mutexes_.size(); ++i)
        	openssl_mutexes_[i].reset(new boost::mutex());
        CRYPTO_set_locking_callback(&backend::openssl_set_lock);
        CRYPTO_set_id_callback(&backend::openssl_set_id);

		LogMessage("S3 backend created");
	}

    static void openssl_set_lock(int mode, int n, const char* /*file*/, int /*line*/)
    {
      if (mode & CRYPTO_LOCK)
    	  boost::static_pointer_cast< backend >(get_instance())->openssl_mutexes_[n]->lock();
      else
    	  boost::static_pointer_cast< backend >(get_instance())->openssl_mutexes_[n]->unlock();
    }

    static unsigned long openssl_set_id()
    {
    	unsigned long ret = (unsigned long)pthread_self();
    	return ret;
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

	struct multidelete_callback_data
	{
		const unsigned int nKeys;

		S3Status status;
	    int errorsCount;
	    int resultsCount;
		std::vector< DeleteMultipleObjectSingleResult * > results;

		multidelete_callback_data(unsigned int nKeys) : nKeys(nKeys), status(S3StatusInternalError), errorsCount(0), resultsCount(0), results(nKeys)
		{
			while(nKeys--)
				results[nKeys] = (DeleteMultipleObjectSingleResult*) malloc(sizeof(DeleteMultipleObjectSingleResult));
		}

		~multidelete_callback_data()
		{
			unsigned int n = nKeys;
			while(n--) {
				free(results[n]); results[n]=0;
			}
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
			for(int i = 0; i < properties->metaDataCount; ++i) {
				if(0 == strncmp(properties->metaData[i].name, S3_METADATA_HEADER_NAME_PREFIX, sizeof(S3_METADATA_HEADER_NAME_PREFIX)-1))
					data->headers[std::string(properties->metaData[i].name, sizeof(S3_METADATA_HEADER_NAME_PREFIX) - 1, std::string::npos)] = properties->metaData[i].value;
				else
					data->headers[properties->metaData[i].name] = properties->metaData[i].value;
			}
		}

		RML_DEBUG_START
		const string_map_t::const_iterator& end(data->headers.end());
		for(string_map_t::const_iterator itr = data->headers.begin(); itr != end; ++itr)
			LogDebug("HEAD: "<<itr->first<<": "<<itr->second);
		RML_DEBUG_END

		return S3StatusOK;
	}

	static S3Status get_response_properties_cb(const S3ResponseProperties *properties, void *callbackData)
	{
		return S3StatusOK;
	}

	static S3Status multidelete_response_properties_cb(const S3ResponseProperties *properties, void *callbackData)
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

		if(error) {
			if(error->message)
				LogMessage("Error::Message: "<<error->message);

			if(error->resource)
				LogMessage("Error::Resource: "<<error->resource);

			if(error->furtherDetails)
				LogMessage("Error::FurtherDetails: "<<error->furtherDetails);

			if(error->extraDetailsCount) {
				LogMessage("Error::ExtraDetails:");
				int i;
				for (i = 0; i < error->extraDetailsCount; i++)
					LogMessage("Error::ExtraDetails::"<<error->extraDetails[i].name << ": " << error->extraDetails[i].value);
			}
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


	static void multidelete_response_complete_cb(S3Status status, const S3ErrorDetails *error, void *callbackData)
	{
		multidelete_callback_data *data = (multidelete_callback_data *) callbackData;
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
        CRYPTO_set_locking_callback(0);
        CRYPTO_set_id_callback(0);

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
	
	inline_ void get(
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

		std::string leg_uuid;
		{
			const types::metadata::pointer& p_metadata( opdata->p_detail->p_metadata );
			types::metadata::const_iterator itr = p_metadata->find("LegUUID");
			if(itr != p_metadata->end())
				leg_uuid = itr->second;
		}

		put(opdata->p_detail->p_endpoint, std::string(opdata->p_detail->identifier), tmpfs,
				opdata->p_detail->target_file_format, headers, opdata->p_detail->clear_size, opdata->p_detail->real_size,
				opdata->p_detail->policy.get_creation_ymd(), opdata->p_detail->policy.get_min_retention_ymd(),
				opdata->p_detail->policy.get_max_retention_ymd(),
				opdata->p_detail->policy.p_policy->id, opdata->p_detail->policy.p_policy->version,
				leg_uuid
				);
	}

	inline_ void put(
			basic_endpoint::pointer p_endpoint,
			const std::string& identifier,
			rml::utils::tmp_fstream::pointer tmpfs,
			const types::file_format::file_format_t& target_file_format,
			const types::caseless_key_string_map& headers,
			const std::size_t& clear_size,
			const std::size_t& real_size,
			const applied_policy::ymd_t& creation_date,
			const applied_policy::ymd_t& retention_min_date,
			const applied_policy::ymd_t& retention_max_date,
			const policy::id_type& policy_id, const policy::version_type& policy_version,
			const std::string& leg_uuid)
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
					types::data_sub_format::to_string(
							types::data_sub_format::data_sub_format_t(target_file_format & 0xff))));
			
			s3headers.insert(std::make_pair(
					"clear-size",
					boost::lexical_cast<std::string>(clear_size)));
			s3headers.insert(std::make_pair("real-size", boost::lexical_cast<std::string>(real_size)));

#define PadZeroTwoDigit(num) \
	(num <= 9? "0": "")+boost::lexical_cast<std::string>(num)

#define YmdToString(ymd) \
	boost::lexical_cast<std::string>(ymd.year)+'-'+PadZeroTwoDigit(ymd.month.as_number())+'-'+PadZeroTwoDigit(ymd.day)

			s3headers.insert(std::make_pair("creation-date", YmdToString(creation_date)));
			s3headers.insert(std::make_pair("retention-min-date", YmdToString(retention_min_date)));
			s3headers.insert(std::make_pair("retention-max-date", YmdToString(retention_max_date)));

			s3headers.insert(std::make_pair("policy-id", boost::lexical_cast<std::string>(policy_id)));
			s3headers.insert(std::make_pair("policy-version", boost::lexical_cast<std::string>(policy_version)));
			s3headers.insert(std::make_pair("leg-uuid", leg_uuid));
		}


		S3NameValue metadata[s3headers.size()];
		LogDebug(identifier<<": Adding "<<s3headers.size()<<" metadata headers");
		int i = 0;
		for(types::caseless_key_string_map::const_iterator itr = s3headers.begin(); itr != s3headers.end(); ++itr) {
			if(!itr->first.empty() && !itr->second.empty()) {
				metadata[i].name = &(*itr).first[0];
				metadata[i].value = &(*itr).second[0];

				LogVerbose(identifier<<": Metadata["<<i<<"] - "<<metadata[i].name<<": "<< metadata[i].value);

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

		LogMessage(identifier<<": HEAD Result Status: "<<S3_get_status_name(data.status));

		if(data.status != S3StatusOK)
			throw rml::exception("HEAD failed");
	}

	void remove(operation_data::pointer opdata) __attribute_deprecated__
	{
		assert(false);
	}

	template < typename Iter, typename ExtractPEndpoint, typename ExtractIdentifier >
	void remove_multiple(Iter begin, Iter end)
	{
		assert(begin != end);

		S3BucketContext bucket_context;

		boost::shared_ptr< endpoint > s3_endpoint = boost::static_pointer_cast< endpoint >(
				ExtractPEndpoint()(*begin));
		bucket_context.bucketName = &s3_endpoint->bucket[0];
		bucket_context.accessKeyId = &s3_endpoint->access_key[0];
		bucket_context.secretAccessKey = &s3_endpoint->secret_key[0];
		if(s3_endpoint->type == endpoint_type::s3_compatible) {
			bucket_context.protocol = S3ProtocolHTTP;
			bucket_context.uriStyle = S3UriStylePath;
			bucket_context.baseHostname = &((s3_compatible::endpoint*)(s3_endpoint.get()))->host[0];
			bucket_context.serviceURI = &((s3_compatible::endpoint*)(s3_endpoint.get()))->service_uri[0];
		}
		else {
			bucket_context.protocol = ( (confMap_["s3_backend.ssl_enabled"].as<std::string>() == "true")? S3ProtocolHTTPS : S3ProtocolHTTP );
			bucket_context.uriStyle = S3UriStyleVirtualHost;
			bucket_context.baseHostname = 0;
			bucket_context.serviceURI = 0;
		}

		S3ResponseHandler multidel_handler;
		multidel_handler.completeCallback = &multidelete_response_complete_cb_;
		multidel_handler.propertiesCallback = &multidelete_response_properties_cb_;

		unsigned int nKeys = 0;
		char * keys[1000]; // TODO batch at 1000s!!!
		ExtractIdentifier ex_identifier;
		for(Iter i = begin; i != end; ++i)
			keys[nKeys++] = strdup(((std::string)(ex_identifier(*i))).c_str());

	    multidelete_callback_data data(nKeys);

		int retry = 2;
	    do {

	        S3_delete_multiple_objects(&bucket_context, nKeys, const_cast<const char**>(keys),
	        		&data.results[0], &data.resultsCount, &data.errorsCount,
	        		0,
	        		&multidel_handler, &data);
	    } while (data.status != S3StatusOK && S3_status_is_retryable(data.status) && --retry);

	    // TODO handle individual key (s3 object) status
		LogMessage("DELETE-MULTI Result Status: "<<S3_get_status_name(data.status));

		if(data.status != S3StatusOK)
			LogError("DELETE-MULTI failed");

		if(data.errorsCount) {
			unsigned int resCount(data.resultsCount);
			while(resCount--)
				if(data.results[resCount]->status)
					LogWarning(std::string(data.results[resCount]->key, data.results[resCount]->keyLen)<<" failed to purge; status:"<<S3_get_status_name(data.results[resCount]->status));
		}

		while(nKeys--) {
			free(keys[nKeys]);
			keys[nKeys] = 0;
		}
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

S3Status multidelete_response_properties_cb_(const S3ResponseProperties *properties, void *callbackData)
{
	S3Status ret = S3StatusErrorUnknown;

	try {
		ret = app::storage::s3::backend::multidelete_response_properties_cb(properties, callbackData);
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

void multidelete_response_complete_cb_(S3Status status, const S3ErrorDetails *error, void *callbackData)
{
	try {
		app::storage::s3::backend::multidelete_response_complete_cb(status, error, callbackData);
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

namespace std {

inline ostream& operator << (ostream& ostr, const app::storage::string_map_t& strmap)
{
	ostr << '{';
	app::storage::string_map_t::const_iterator begin(strmap.begin());
	app::storage::string_map_t::const_iterator end(strmap.end());
	for(app::storage::string_map_t::const_iterator itr = strmap.begin(); itr != end; ++itr)
		ostr << (itr==begin? "":", ")<<itr->first << ':' << itr->second;
	ostr << '}';

	return ostr;
}

}  // namespace std

#endif /* BASIC_STORAGE_BACKEND_HPP_ */
