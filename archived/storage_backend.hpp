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
S3Status response_properties_cb_(const S3ResponseProperties *properties, void *callbackData);
void response_complete_cb_(S3Status status, const S3ErrorDetails *error, void *callbackData);
int put_object_data_cb_(int bufferSize, char *buffer, void *callbackData);
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

//namespace specific {
//
//class initialiser {
//	mutable boost::mutex mutex_;
//	boost::condition_variable cond_initialised_;
//
//	struct not_initialised {
//		const initialiser& initialiser_;
//
//		not_initialised(const initialiser& initialiser) : initialiser_(initialiser)
//		{
//		}
//
//		bool operator() () const {
//			return !initialiser_.initialised_;
//		}
//	};
//
//	friend struct not_initialised;
//
//protected:
//	bool initialised_;
//
//public:
//	initialiser() : initialised_(false)
//	{
//		boost::unique_lock<boost::mutex> lock(mutex_);
//		S3Status status = S3_initialize("RedMatterArchive", S3_INIT_ALL);
//		if(status != S3StatusOK)
//			throw rml::exception("S3 initialisation failed");
//
//		initialised_ = true;
//		cond_initialised_.notify_all();
//	}
//
//	static void destroy()
//	{
//		static bool first = true;
//		if(first) {
//			first = false;
//			S3_deinitialize();
//		}
//	}
//
//	void wait()
//	{
//		boost::unique_lock<boost::mutex> lock(mutex_);
//		cond_initialised_.wait(lock, not_initialised(*this));
//	}
//
//	~initialiser()
//	{
//		destroy();
//	}
//};
//
//} //namespace specific

class backend : public basic_backend< backend >
{
	const boost::program_options::variables_map& confMap_;

	friend S3Status ::response_properties_cb_(const S3ResponseProperties*, void*);
	friend void ::response_complete_cb_(S3Status, const S3ErrorDetails*, void*);
	friend int ::put_object_data_cb_(int, char*, void*);

protected:
	backend(const boost::program_options::variables_map& confMap) :
		confMap_(confMap)
	{
		LogMessage("S3 backend created");
	}

	virtual ~backend()
	{
		LogMessage("S3 backend destroyed");
	}

	struct put_callback_data
	{
		rml::utils::tmp_fstream::pointer p_tmpfs_;
		std::size_t to_be_copied;
		S3Status status;
		std::string base64_md5;

		put_callback_data() : p_tmpfs_(), to_be_copied(0)
		{
		}

		put_callback_data(rml::utils::tmp_fstream::pointer p_tmpfs) :
			p_tmpfs_(p_tmpfs),
			to_be_copied(p_tmpfs->get_size())
		{
			p_tmpfs_->seekg(0);
			CryptoPP::Weak1::MD5 hash;
			CryptoPP::FileSource f(*p_tmpfs_, true,
					new CryptoPP::HashFilter(hash,
							new CryptoPP::Base64Encoder(
									new CryptoPP::StringSink(base64_md5), false)));

			if(!(*p_tmpfs_) && !p_tmpfs_->eof())
				throw rml::exception("Error while creating md5");

			LogVerbose("Base64(MD5): "<<base64_md5);

			p_tmpfs_->clear(); // clear eof
			p_tmpfs_->seekg(0);
		}
	};

	static S3Status response_properties_cb(const S3ResponseProperties *properties, void *callbackData)
	{
		string_map_t& p_str_map(*(string_map_t *)callbackData);
		(void)p_str_map;

		return S3StatusOK;
	}

	static void response_complete_cb(S3Status status, const S3ErrorDetails *error, void *callbackData)
	{
		put_callback_data *data = (put_callback_data *) callbackData;
		data->status = status;

		LogVerbose("status: "<<S3_get_status_name(status));

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

public:

	friend class basic_backend;

	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("s3_backend.ssl_enabled",  boost::program_options::value<std::string>()->default_value("true"), "Use SSL for S3 connection")
				;
	}

	void get(operation_data::pointer opdata)
	{
//		if(opdata->state != operation_states::get_file_retrieve)
//			THROW_RML_EXCEPTION(opdata->p_detail->p_endpoint->bucket<<'/'<<opdata->p_detail->identifier<<": invalid state");
//
//		try {
//			types::file_format::file_format_t s3_fileformat;
//			types::file_format::file_format_t target_fileformat;
//			{
//				aws::HeadResponsePtr p_headRes = p_s3Conn->head(opdata->p_detail->p_endpoint->bucket, opdata->p_detail->identifier);
//				const std::map<std::string, std::string>& metadata(p_headRes->getMetaData());
//				std::map<std::string, std::string>::const_iterator itr_is_compressed = metadata.find("file-type-is-compressed");
//				std::map<std::string, std::string>::const_iterator itr_is_encrypted  = metadata.find("file-type-is-encrypted");
//				std::map<std::string, std::string>::const_iterator itr_filetype  = metadata.find("file-type");
//				std::map<std::string, std::string>::const_iterator itr_end = metadata.end();
//				types::data_sub_format::data_sub_format_t dsf = types::data_sub_format::binary;
//				if(itr_filetype != itr_end) {
//					dsf = types::data_sub_format::from_string(itr_filetype->second);
//					if(dsf == types::data_sub_format::unknown) {
//						LogError(opdata->p_detail->p_endpoint->bucket<<'/'<<opdata->p_detail->identifier<<": invalid header rm-file-type: "<<itr_filetype->second<<"; using 'binary' instead");
//						dsf = types::data_sub_format::binary;
//					}
//				}
//				else
//					LogError(opdata->p_detail->p_endpoint->bucket<<'/'<<opdata->p_detail->identifier<<": NO header rm-file-type; using 'binary'");
//
//				s3_fileformat = types::file_format::make(
//						((itr_is_compressed != itr_end) && (itr_is_compressed->second == "true")),
//						((itr_is_encrypted != itr_end) && (itr_is_encrypted->second == "true")),
//						dsf);
//
//				target_fileformat = types::file_format::make(false, false, dsf);
//			}
//
//			rml::utils::tmp_fstream::pointer fstr = opdata->get_tmp_fstream(s3_fileformat, true, true); // create & force new
//			{
//				aws::GetResponsePtr p_getRes = p_s3Conn->get(opdata->p_detail->p_endpoint->bucket, opdata->p_detail->identifier);
//				*fstr << p_getRes->getInputStream().rdbuf();
//			}
//
//			opdata->p_detail->current_file_format = s3_fileformat;
//			opdata->p_detail->target_file_format = target_fileformat;
//		}
//		catch(const aws::HeadException& e) {
//			THROW_RML_EXCEPTION(opdata->p_detail->p_endpoint->bucket<<'/'<<opdata->p_detail->identifier<<" - GET failed - (HEAD) error:"<<e.what());
//		}
//		catch(const aws::GetException& e) {
//			THROW_RML_EXCEPTION(opdata->p_detail->p_endpoint->bucket<<'/'<<opdata->p_detail->identifier<<" - GET failed - (GET) error:"<<e.what());
//		}
	}

	void put(operation_data::pointer opdata)
	{
		endpoint::pointer p_endpoint( boost::static_pointer_cast<endpoint>(opdata->p_detail->p_endpoint) );

		if(opdata->p_detail->state != operation_state::put_file_save && opdata->p_detail->retry > 0)
			THROW_RML_EXCEPTION(p_endpoint->bucket<<'/'<<opdata->p_detail->identifier<<": invalid state (retry: "<<opdata->p_detail->retry<<')');

		rml::utils::tmp_fstream::pointer tmpfs = opdata->get_tmp_fstream(opdata->p_detail->target_file_format, false, false);
		if(!tmpfs)
			THROW_RML_EXCEPTION(p_endpoint->bucket<<'/'<<opdata->p_detail->identifier<<": No target format found in opdata");

		assert(false == opdata->p_detail->p_metadata->empty());
		types::caseless_key_string_map headers;
		opdata->p_detail->p_metadata->make_headers(headers);

		put(p_endpoint, std::string(opdata->p_detail->identifier), tmpfs, opdata->p_detail->target_file_format, headers);
	}

	inline void put(
			endpoint::pointer p_endpoint,
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
		LogVerbose(identifier<<": Adding "<<s3headers.size()<<" metadata headers");
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
		bucket_context.bucketName = &p_endpoint->bucket[0];
		bucket_context.protocol = ( (confMap_["s3_backend.ssl_enabled"].as<std::string>() == "true")? S3ProtocolHTTPS : S3ProtocolHTTP );
		bucket_context.uriStyle = S3UriStyleVirtualHost;
		bucket_context.accessKeyId = &p_endpoint->access_key[0];
		bucket_context.secretAccessKey = &p_endpoint->secret_key[0];

		S3PutObjectHandler put_handler;
		put_handler.responseHandler.completeCallback = &response_complete_cb_;
		put_handler.responseHandler.propertiesCallback = &response_properties_cb_;
		put_handler.putObjectDataCallback = &put_object_data_cb_;

		int retry = 2;
		do {
			S3_put_object(&bucket_context, &identifier[0], data.to_be_copied, &put_props, 0, &put_handler, &data);
		} while (data.status != S3StatusOK && S3_status_is_retryable(data.status) && --retry);

		LogVerbose(identifier<<": PUT Result Status: "<<S3_get_status_name(data.status));

		if(data.status != S3StatusOK)
			throw rml::exception("S3 PUT failed");
	}

	void stat(operation_data::pointer opdata, string_map_t& res)
	{

	}

	void remove(operation_data::pointer opdata)
	{

	}

};

}  // namespace s3

}  // namespace storage

}  // namespace app

extern "C" {

S3Status response_properties_cb_(const S3ResponseProperties *properties, void *callbackData)
{
	S3Status ret = S3StatusErrorUnknown;

	try {
		ret = app::storage::s3::backend::response_properties_cb(properties, callbackData);
	}
	catch(const std::exception& e) {
		LogError(e.what());
	}
	catch(...) {
		LogError("Unexpected exception");
	}

	return ret;
}

void response_complete_cb_(S3Status status, const S3ErrorDetails *error, void *callbackData)
{
	try {
		app::storage::s3::backend::response_complete_cb(status, error, callbackData);
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

}

#endif /* BASIC_STORAGE_BACKEND_HPP_ */
