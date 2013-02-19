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
#include "../common/types.hpp"

#include <cryptopp/cryptlib.h>
#include <cryptopp/filters.h>
#include <cryptopp/modes.h>
#include <cryptopp/base64.h>

#include <boost/shared_ptr.hpp>

namespace app {

namespace storage {

struct endpoint_type
{
	enum endpoint_type_t
	{
		unknown = 0,
		s3,
		file_system,
		s3_compatible,

		n_endpoint_types
	};

	static inline_ endpoint_type_t from_string(const std::string& str)
	{
		if(str == "s3")
			return s3;
		else if(str == "file-system")
			return file_system;
		else if(str == "s3-compatible" || str == "s3-emulation")
			return s3_compatible;

		else
			return unknown;
	}

	static inline_ const char* to_string(const endpoint_type_t t)
	{
		static const char* str[n_endpoint_types] = {
				"unknown",
				"s3",
				"file-system",
				"s3-compatible"
		};

		if(t > unknown && t < n_endpoint_types)
			return str[t];

		return str[0];
	}
};

struct basic_endpoint
{
	typedef boost::array<uint8_t, 32> encryption_key_t;
	typedef boost::shared_ptr<basic_endpoint> pointer;

	const archiving::endpoint_id_t id;
	const endpoint_type::endpoint_type_t type;

	const archiving::org_id_t org;
	const std::string name;

	const bool is_encrypted;
	const bool is_compressed;

	encryption_key_t encryption_key;

	basic_endpoint(
			const endpoint_type::endpoint_type_t type,
			const uint64_t& identifier,
			const archiving::org_id_t& org,
			const std::string& name,
			const bool is_encrypted,
			const bool is_compressed,
			const encryption_key_t& encryption_key) :
		id(identifier), type(type), org(org), name(name),
		is_encrypted(is_encrypted), is_compressed(is_compressed),
		encryption_key(encryption_key)
	{
		//ctor
	}

	basic_endpoint(const basic_endpoint& t) :
		id(t.id), type(t.type), org(t.org), name(t.name), is_encrypted(t.is_encrypted), is_compressed(t.is_compressed), encryption_key(t.encryption_key)
	{
		//ctor
	}

	bool operator==(const basic_endpoint& rhs) const
	{
		return type == rhs.type &&
				org == rhs.org &&
				is_encrypted == rhs.is_encrypted &&
				is_compressed == rhs.is_compressed &&
				encryption_key == rhs.encryption_key;
	}

	friend std::ostream& operator<<(std::ostream& os, const basic_endpoint& be)
	{
		return os << "id:"<<be.id<<", org:"<<be.org<<", name:"<<be.name<<", encrypted:"<<RML_YES_NO(be.is_encrypted)<<", compressed:"<<RML_YES_NO(be.is_compressed);
	}
};

namespace file_system {

struct endpoint : public basic_endpoint
{
	const bool is_slow_medium;
	const std::string base_path;

	typedef boost::shared_ptr<endpoint> pointer;

	endpoint(
			const uint64_t& id,
			const archiving::org_id_t& org,
			const std::string& name,
			const bool& is_encrypted,
			const bool& is_compressed,
			const encryption_key_t& encryption_key,
			const std::string& base_path,
			const bool& is_slow_medium) :
		basic_endpoint(endpoint_type::s3, id, org, name, is_encrypted, is_compressed, encryption_key),
		is_slow_medium(is_slow_medium), base_path(base_path)
	{
	}

	endpoint(const endpoint& t) :
		basic_endpoint(static_cast<basic_endpoint>(t)),
		is_slow_medium(t.is_slow_medium)
	{
	}

	bool operator==(const endpoint& rhs) const
	{
		return static_cast<basic_endpoint>(rhs) == static_cast<basic_endpoint>(*this) &&
				rhs.base_path == base_path
				;
	}

	std::ostream& operator<<(std::ostream& os)
	{
		return os << "file://"<<base_path<<" ("<<static_cast<basic_endpoint>(*this)<<",slow-medium: "<<RML_YES_NO(is_slow_medium)<<")";
	}
};

}  // namespace file_system

namespace s3 {

struct endpoint : public basic_endpoint
{
	const std::string access_key;
	const std::string secret_key;
	const std::string bucket;

	typedef boost::shared_ptr<endpoint> pointer;

	endpoint(
			const uint64_t& id,
			const archiving::org_id_t& org,
			const std::string& name,
			const bool& is_encrypted,
			const bool& is_compressed,
			const encryption_key_t& encryption_key,
			const std::string& access_key,
			const std::string& secret_key,
			const std::string& bucket) :
		basic_endpoint(endpoint_type::s3, id, org, name, is_encrypted, is_compressed, encryption_key),
		access_key(access_key), secret_key(secret_key), bucket(bucket)
	{
	}

	endpoint(const endpoint& t) :
		basic_endpoint(static_cast<basic_endpoint>(t)),
		access_key(t.access_key), secret_key(t.secret_key), bucket(t.bucket)
	{
	}

	bool operator==(const endpoint& rhs) const
	{
		return static_cast<basic_endpoint>(rhs) == static_cast<basic_endpoint>(*this) &&
				access_key == rhs.access_key &&
				secret_key == rhs.secret_key &&
				bucket == rhs.bucket
				;
	}

	std::ostream& operator<<(std::ostream& os)
	{
		return os << "s3://**ak**:**sk**@"<<bucket<<" ("<<static_cast<basic_endpoint>(*this)<<")";
	}
};

}  // namespace s3

namespace s3_compatible {

struct endpoint : public basic_endpoint
{
	const std::string access_key;
	const std::string secret_key;
	const std::string bucket;

	const std::string host;
	const std::string service_uri;

	typedef boost::shared_ptr<endpoint> pointer;

	endpoint(
			const uint64_t& id,
			const archiving::org_id_t& org,
			const std::string& name,
			const bool is_encrypted,
			const bool is_compressed,
			const encryption_key_t& encryption_key,
			const std::string& access_key,
			const std::string& secret_key,
			const std::string& bucket,
			const std::string& host,
			const std::string& service_uri) :
				basic_endpoint(endpoint_type::s3_compatible, id, org, name, is_encrypted, is_compressed, encryption_key),
				access_key(access_key), secret_key(secret_key), bucket(bucket),
				host(host), service_uri(service_uri)
	{
	}

	endpoint(const endpoint& t) :
		basic_endpoint(static_cast<basic_endpoint>(t)),
		access_key(t.access_key), secret_key(t.secret_key), bucket(t.bucket),
		host(t.host), service_uri(t.service_uri)
	{
	}

	bool operator==(const endpoint& rhs) const
	{
		return static_cast<basic_endpoint>(rhs) == static_cast<basic_endpoint>(*this) &&
				access_key == rhs.access_key &&
				secret_key == rhs.secret_key &&
				bucket == rhs.bucket &&
				host == rhs.host &&
				service_uri == rhs.service_uri
				;
	}

	std::ostream& operator<<(std::ostream& os)
	{
		return os << "s3c://**ak**:**sk**@"<<host<<service_uri<<'/'<<bucket<<" ("<<static_cast<basic_endpoint>(*this)<<")";
	}

};

}  // namespace s3_compatible


inline basic_endpoint::pointer endpoint_finder(coreapi& api, const std::pair< archiving::org_id_t, archiving::endpoint_id_t >& endpoint_def)
{
	const archiving::org_id_t& org_id(endpoint_def.first);
	const archiving::endpoint_id_t& endpoint_id(endpoint_def.second);

	std::ostringstream ostr;

	ostr << "/archiving/endpoint/" << org_id << '/' << endpoint_id;

	const coreapi::response& response(api.get(ostr.str(), 200, true));

	// run xpath
	pugi::xml_document doc;
	if(!doc.load(response.second.c_str()))
		throw rml::exception("Unable to load response document");

	pugi::xml_node endpoint_node;
	{
		pugi::xpath_node endpoint_xpath_node = doc.select_single_node("/Archiving/Endpoint");
		if(endpoint_xpath_node)
			endpoint_node = endpoint_xpath_node.node();
		else {
			LogCritical("Invalid response from Core API; /Archiving/Endpoint NOT found");
			throw rml::exception("Invalid response from Core API (Invalid Endpoint XML)");
		}
	}

	const char* id_str = endpoint_node.select_single_node("ID/text()").node().value();
	const char* name_str = endpoint_node.select_single_node("Name/text()").node().value();

	const char* org_id_str = endpoint_node.select_single_node("OrgID/text()").node().value();
	const archiving::org_id_t& org_id_xml = rml::utils::strtoull(org_id_str, 0ULL);

	const char* is_encrypted_str = endpoint_node.select_single_node("Encrypted/text()").node().value();
	const char* is_compressed_str = endpoint_node.select_single_node("Compressed/text()").node().value();


	basic_endpoint::encryption_key_t enc_key;
	const char* encryption_key = endpoint_node.select_single_node("EncryptionKey/text()").node().value();
	CryptoPP::StringSource((const byte*)encryption_key, strlen(encryption_key), true,
			new CryptoPP::Base64Decoder(
					new CryptoPP::ArraySink(&enc_key[0], basic_endpoint::encryption_key_t::static_size)));

	basic_endpoint::pointer ret;

	const char* type_str = endpoint_node.select_single_node("Type/text()").node().value();
	if (!(type_str && type_str[0])) {
		LogCritical("Invalid response from Core API; /Archiving/Endpoint/Type NOT found");
		throw rml::exception("Invalid response from Core API (Endpoint Type NOT found)");
	}

	const endpoint_type::endpoint_type_t& type = endpoint_type::from_string(type_str);
	switch(type) {

	case endpoint_type::s3:
	{
		const char* access_key = endpoint_node.select_single_node("AccessKey/text()").node().value();
		const char* secret_key = endpoint_node.select_single_node("SecretKey/text()").node().value();
		const char* bucket = endpoint_node.select_single_node("Bucket/text()").node().value();
		ret.reset(((basic_endpoint*)new s3::endpoint(
				rml::utils::strtoull(id_str),
				org_id_xml,
				name_str,
				0==strcmp(is_encrypted_str, "yes"),
				0==strcmp(is_compressed_str, "yes"),
				enc_key,
				access_key,
				secret_key,
				bucket
				)));

		break;
	}

	case endpoint_type::file_system:
	{
		const char* base_path = endpoint_node.select_single_node("BasePath/text()").node().value();
		const char* slow_medium = endpoint_node.select_single_node("SlowMedium/text()").node().value();
		ret.reset(((basic_endpoint*)new file_system::endpoint(
				rml::utils::strtoull(id_str),
				org_id_xml,
				name_str,
				0==strcmp(is_encrypted_str, "yes"),
				0==strcmp(is_compressed_str, "yes"),
				enc_key,
				base_path,
				0==strcmp(slow_medium, "yes")
				)));

		break;
	}

	case endpoint_type::s3_compatible:
	{
		const char* access_key = endpoint_node.select_single_node("AccessKey/text()").node().value();
		const char* secret_key = endpoint_node.select_single_node("SecretKey/text()").node().value();
		const char* bucket = endpoint_node.select_single_node("Bucket/text()").node().value();

		const std::string base_uri( endpoint_node.select_single_node("BaseURI/text()").node().value() );
		std::string host, service_uri;
		{
			const std::size_t& slash_pos = base_uri.find('/');
			const std::size_t& last_slash_pos = base_uri.rfind('/');
			if( slash_pos != std::string::npos ) {
				host = base_uri.substr(0, slash_pos);
				service_uri = base_uri.substr(slash_pos);
				if(last_slash_pos == slash_pos || last_slash_pos != (base_uri.length() - 1))
					service_uri += '/';
			}
			else {
				host = base_uri;
				service_uri = "/";
			}
		}

		ret.reset(((basic_endpoint*)new s3_compatible::endpoint(
				rml::utils::strtoull(id_str),
				org_id_xml,
				name_str,
				0==strcmp(is_encrypted_str, "yes"),
				0==strcmp(is_compressed_str, "yes"),
				enc_key,
				access_key,
				secret_key,
				bucket,
				host,
				service_uri
				)));

		break;
	}

	default:
		THROW_RML_EXCEPTION("Unknown endpoint-type: "<<type_str<<" id: "<<id_str);
		/* no break */
	}


	return ret;
}

typedef cache::result_cache<const std::pair< archiving::org_id_t, archiving::endpoint_id_t >&, basic_endpoint::pointer> cached_endpoint_finder_t;


}  // namespace storage

}  // namespace app

#endif /* STORAGE_ENDPOINT_HPP_ */
