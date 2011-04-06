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
#include "storage_object_type.hpp"
#include "storage_endpoint.hpp"

#include <rml/exception.h>

#include <libs3.h>

#include <boost/noncopyable.hpp>

#include <fstream>
#include <string>
#include <map>

#include <stdint.h>

extern "C" {
S3Status response_properties_cb_(const S3ResponseProperties *properties, void *callbackData);
void response_complete_cb_(S3Status status, const S3ErrorDetails *error, void *callbackData);
int put_object_data_cb_(int bufferSize, char *buffer, void *callbackData);
}

namespace app {

namespace storage {

template < typename Backend >
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

	virtual ~basic_backend()
	{
		//virtual do nothing!
	};

};

template < typename Backend >
Backend* basic_backend< Backend >::p_backend_(0);

// specialisation

namespace s3 {

namespace specific {

template < typename ignore >
class initialiser {
	mutable boost::mutex mutex_;
	boost::condition_variable cond_initialised_;

	struct not_initialised {
		const initialiser& initialiser_;

		not_initialised(const initialiser& initialiser) : initialiser_(initialiser)
		{
		}

		bool operator() () const {
			return !initialiser_.initialised_;
		}
	};

	friend struct not_initialised;

protected:
	bool initialised_;

public:
	static initialiser< ignore > static_initialiser;

	initialiser() : mutex_(), cond_initialised_(mutex_), initialised_(false)
	{
		boost::unique_lock<boost::mutex> lock(mutex_);
		S3Status status = S3_initialize("RedMatterArchive", S3_INIT_ALL);
		if(status != S3StatusOK)
			throw rml::exception("S3 initialisation failed");

		initialised_ = true;
		cond_initialised_.notify_all();
	}

	static void destroy()
	{
		static bool first = true;
		if(first) {
			first = false;
			S3_deinitialize();
		}
	}

	void wait()
	{
		boost::unique_lock<boost::mutex> lock(mutex_);
		cond_initialised_.wait(lock, not_initialised(*this));
	}

	~initialiser()
	{
		destroy();
	}
};

template < typename ignore >
initialiser< ignore > initialiser< ignore >::static_initialiser;

} //namespace specific

typedef specific::initialiser<void> initialiser;

class backend : public basic_backend<backend>
{
	const boost::program_options::variables_map& confMap_;

	friend S3Status response_properties_cb_(const S3ResponseProperties*, void*);
	friend void response_complete_cb_(S3Status, const S3ErrorDetails*, void*);
	friend int put_object_data_cb_(int, char*, void*);

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

		put_callback_data() : p_tmpfs_(), to_be_copied(0)
		{
		}

		put_callback_data(rml::utils::tmp_fstream::pointer p_tmpfs) : p_tmpfs_(p_tmpfs), to_be_copied(p_tmpfs->get_size())
		{
			p_tmpfs_->seekg(0);
		}
	};

	static S3Status response_properties_cb(const S3ResponseProperties *properties, void *callbackData)
	{
		(void) callbackData;
		return S3StatusOK;
	}

	static void response_complete_cb(S3Status status, const S3ErrorDetails *error, void *callbackData)
	{
		put_callback_data *data = (put_callback_data *) callbackData;
		data->status = status;

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

	friend class basic_backend<backend>;

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
//				std::map<std::string, std::string>::const_iterator itr_is_compressed = metadata.find("FileTypeIsCompressed");
//				std::map<std::string, std::string>::const_iterator itr_is_encrypted  = metadata.find("FileTypeIsEncrypted");
//				std::map<std::string, std::string>::const_iterator itr_filetype  = metadata.find("FileType");
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
		if(opdata->state != operation_states::put_file_save && opdata->p_detail->retry > 0)
			THROW_RML_EXCEPTION(opdata->p_detail->p_endpoint->bucket<<'/'<<opdata->p_detail->identifier<<": invalid state (retry: "<<opdata->p_detail->retry<<')');

		rml::utils::tmp_fstream::pointer tmpfs = opdata->get_tmp_fstream(opdata->p_detail->target_file_format, false, false);
		if(!tmpfs)
			THROW_RML_EXCEPTION(opdata->p_detail->p_endpoint->bucket<<'/'<<opdata->p_detail->identifier<<": No target format found in opdata");

		assert(false == opdata->p_detail->p_metadata->empty());
		std::map< std::string, std::string > headers;
		opdata->p_detail->p_metadata->make_headers(headers);

		put(opdata->p_detail->p_endpoint, opdata->p_detail->identifier, tmpfs, opdata->p_detail->target_file_format, headers);
	}

	typedef std::map < std::string, std::string > string_string_map;

	inline void put(
			endpoint::pointer p_endpoint,
			const std::string& identifier,
			rml::utils::tmp_fstream::pointer tmpfs,
			const types::file_format::file_format_t& target_file_format,
			const string_string_map& headers = string_string_map())
	{
		string_string_map s3headers = headers;
		{
			s3headers.insert(std::make_pair(
					"FileTypeIsCompressed",
					types::file_format::is_compressed(target_file_format)? "true": "false"));
			s3headers.insert(std::make_pair(
					"FileTypeIsEncrypted",
					types::file_format::is_encrypted(target_file_format)? "true": "false"));
			s3headers.insert(std::make_pair(
					"FileType",
					types::data_sub_format::to_string(types::data_sub_format::data_sub_format_t(target_file_format & 0xff))));
		}

		std::vector<S3NameValue> metadata;
		for(string_string_map::const_iterator itr = s3headers.begin(); itr != s3headers.end(); ++itr) {
			if(!itr->first.empty() && !itr->second.empty()) {
				S3NameValue nv;
				nv.name = &(*itr).first[0];
				nv.value = &(*itr).second[0];
				metadata.push_back(nv);
			}
		}

		S3PutProperties s3pp;
		s3pp.contentType = "application/octet-stream";
		// TODO XXX FIXME add md5
		// s3pp.md5 = 0;
		s3pp.metaDataCount = metadata.size();
		s3pp.metaData = &metadata[0];

		S3BucketContext s3bc;
		s3bc.bucketName = &p_endpoint->bucket[0];
		s3bc.protocol = S3ProtocolHTTPS; // TODO config driven
		s3bc.uriStyle = S3UriStyleVirtualHost;
		s3bc.accessKeyId = &p_endpoint->access_key[0];
		s3bc.secretAccessKey = &p_endpoint->secret_key[0];

		S3PutObjectHandler putHandler;
		putHandler.responseHandler.completeCallback = &response_complete_cb;
		putHandler.responseHandler.propertiesCallback = &response_properties_cb;
		putHandler.putObjectDataCallback = &put_object_data_cb;

		put_callback_data data(tmpfs);

		int retry = 2;
		do {
			S3_put_object(&s3bc, &identifier[0], data.to_be_copied, &s3pp, 0, &putHandler, &data);
		} while (data.status != S3StatusOK && S3_status_is_retryable(data.status) && --retry);

		if(data.status != S3StatusOK)
			throw rml::exception("S3 PUT failed");
	}

};

}  // namespace s3

}  // namespace storage

}  // namespace app

extern "C" {

S3Status response_properties_cb_(const S3ResponseProperties *properties, void *callbackData)
{
	return app::storage::s3::backend::response_properties_cb(properties, callbackData);
}

void response_complete_cb_(S3Status status, const S3ErrorDetails *error, void *callbackData)
{
	app::storage::s3::backend::response_complete_cb(status, error, callbackData);
}

int put_object_data_cb_(int bufferSize, char *buffer, void *callbackData)
{
	return app::storage::s3::backend::put_object_data_cb(bufferSize, buffer, callbackData);
}

}

#endif /* BASIC_STORAGE_BACKEND_HPP_ */
