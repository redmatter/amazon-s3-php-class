/*
 * storage_endpoint.hpp
 *
 *  Created on: 18 Feb 2011
 *      Author: dkorah
 */

#ifndef STORAGE_ENDPOINT_HPP_
#define STORAGE_ENDPOINT_HPP_


#include "../common/coreapi.hpp"
#include "../common/archivable.hpp"
#include "../common/cache.hpp"

#include <cryptopp/cryptlib.h>
#include <cryptopp/filters.h>
#include <cryptopp/modes.h>
#include <cryptopp/base64.h>

namespace app {

namespace storage {

struct endpoint_type
{
	enum endpoint_type_t
	{
		unknown = 0,
		s3,
		file_system,

		n_endpoint_types
	};

	static inline endpoint_type_t from_string(const std::string& str)
	{
		if(str == "s3")
			return s3;
		else if(str == "file-system")
			return file_system;

		else
			return unknown;
	}

	static inline const char* to_string(const endpoint_type_t t)
	{
		static const char* str[n_endpoint_types] = {
				"unknown",
				"s3",
				"file-system"
		};

		if(t > unknown && t < n_endpoint_types)
			return str[t];

		return str[0];
	}
};

struct basic_endpoint
{
	endpoint_type::endpoint_type_t type;
	uint64_t identifier;

	basic_endpoint(const endpoint_type::endpoint_type_t type, const uint64_t& identifier) : type(type), identifier(identifier)
	{
		//ctor
	}

	basic_endpoint(const basic_endpoint& t) : type(t.type), identifier(t.identifier)
	{
		//ctor
	}

	bool operator==(const basic_endpoint& rhs) const
	{
		return type == rhs.type && identifier == rhs.identifier;
	}

	std::ostream& operator<<(std::ostream& os)
	{
		return os << identifier;
	}
};

namespace s3 {

struct endpoint : public basic_endpoint
{
	std::string access_key;
	std::string secret_key;
	std::string bucket;
	std::vector<uint8_t> encryption_key;
	bool is_encrypted;
	bool is_compressed;

	endpoint();
public:
	typedef boost::shared_ptr<endpoint> pointer;

	endpoint(const uint64_t& id, const std::string& access_key, const std::string& secret_key, const std::string& bucket, const std::vector<uint8_t>& encryption_key, const bool is_encrypted, const bool is_compressed) :
		basic_endpoint(endpoint_type::s3, id),
		access_key(access_key), secret_key(secret_key), bucket(bucket), encryption_key(encryption_key),
		is_encrypted(is_encrypted), is_compressed(is_compressed)
	{
	}

	endpoint(const endpoint& t) :
		basic_endpoint(static_cast<basic_endpoint>(t)),
		access_key(t.access_key), secret_key(t.secret_key), bucket(t.bucket), encryption_key(t.encryption_key),
		is_encrypted(t.is_encrypted), is_compressed(t.is_compressed)
	{
	}

	bool operator==(const endpoint& rhs) const
	{
		return type == rhs.type &&
				access_key == rhs.access_key &&
				secret_key == rhs.secret_key &&
				bucket == rhs.bucket &&
				encryption_key == rhs.encryption_key &&
				is_encrypted == rhs.is_encrypted &&
				is_compressed == rhs.is_compressed
				;
	}

	std::ostream& operator<<(std::ostream& os)
	{
		return os << "s3://**ak**:**sk**@"<<bucket;
	}

	static pointer finder(coreapi& api, const archiving::org_id& org_id)
	{
		std::ostringstream ostr;

		// TODO use OrgID and Format ( archiving::archivable::file_type ) so that we can store CDRs to a different EndPoint
		ostr << "/archiving/endpoint/" << org_id;

		const std::string& response = api.get(ostr.str());

		// run xpath
		pugi::xml_document doc;
		if(!doc.load(response.c_str()))
			throw rml::exception("Unable to load response document");

		const char* id_str = doc.select_single_node("/Archiving/Endpoint/ID/text()").node().value();
		assert(id_str && strlen(id_str));

		// TODO use factory to make endpoints
		const char* type_str = doc.select_single_node("/Archiving/Endpoint/Type/text()").node().value();
		assert(type_str && strlen(type_str));
		assert(0 == strcmp(type_str, "s3"));

		const char* is_encrypted_str = doc.select_single_node("/Archiving/Endpoint/Encrypted/text()").node().value();
		assert(is_encrypted_str && strlen(is_encrypted_str));
		const char* is_compressed_str = doc.select_single_node("/Archiving/Endpoint/Compressed/text()").node().value();
		assert(is_compressed_str && strlen(is_compressed_str));

		std::vector<uint8_t> enc_key(32);
		const char* encryption_key = doc.select_single_node("/Archiving/Endpoint/EncryptionKey/text()").node().value();
		assert(encryption_key && strlen(encryption_key));
		CryptoPP::StringSource((const byte*)encryption_key, strlen(encryption_key), true,
				new CryptoPP::Base64Decoder(
						new CryptoPP::ArraySink(&enc_key[0], 32)));

		const char* access_key = doc.select_single_node("/Archiving/Endpoint/AccessKey/text()").node().value();
		assert(access_key && strlen(access_key));
		const char* secret_key = doc.select_single_node("/Archiving/Endpoint/SecretKey/text()").node().value();
		assert(secret_key && strlen(secret_key));
		const char* bucket = doc.select_single_node("/Archiving/Endpoint/Bucket/text()").node().value();
		assert(bucket && strlen(bucket));

		return pointer(new endpoint(rml::utils::strtoull(id_str), access_key, secret_key, bucket, enc_key, 0==strcmp(is_encrypted_str, "yes"), 0==strcmp(is_compressed_str, "yes")));
	}

	typedef cache::result_cache<const archiving::org_id&, pointer> cached_finder_t;
};

}  // namespace s3

}  // namespace storage

}  // namespace app

#endif /* STORAGE_ENDPOINT_HPP_ */
