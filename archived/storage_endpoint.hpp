/*
 * storage_endpoint.hpp
 *
 *  Created on: 18 Feb 2011
 *      Author: dkorah
 */

#ifndef STORAGE_ENDPOINT_HPP_
#define STORAGE_ENDPOINT_HPP_

#include "coreapi.hpp"

namespace app {

namespace storage {

struct endpoint_type {
	enum endpoint_type_t {
		unknown = 0,
		s3,
		file_system,

		n_endpoint_types
	};

	static inline endpoint_type_t from_string(const std::string& str) {
		if(str == "s3")
			return s3;
		else if(str == "file-system")
			return file_system;

		else
			return unknown;
	}

	static inline const char* to_string(const endpoint_type_t t) {
		static const char const* str = {
				"unknown",
				"s3",
				"file-system"
		};

		if(t > unknown && t < n_endpoint_types)
			return str[t];

		return str[0];
	}
};

class basic_endpoint {
	endpoint_type::endpoint_type_t type_;

protected:
	std::string identifier_;

	basic_endpoint();
public:
	basic_endpoint(const endpoint_type::endpoint_type_t type, const std::string& identifier) : type_(type), identifier_(identifier) {}
	basic_endpoint(const basic_endpoint& t) : type_(t.type_), identifier_(t.identifier_) {}
	virtual ~basic_endpoint() {}

	virtual bool operator==(const basic_endpoint& rhs) const {
		return type_ == rhs.type_ && identifier_ == rhs.identifier_;
	}

	virtual std::ostream& operator<<(std::ostream& os) {
		return os << identifier_;
	}
};

namespace s3 {

class endpoint : public basic_endpoint {
	const std::string access_key_;
	const std::string secret_key_;
	const std::string bucket_;
	const std::vector<uint8_t> encryption_key_;

	endpoint();
public:
	typedef boost::shared_ptr<endpoint> pointer;

	endpoint(const std::string& access_key, const std::string& secret_key, const std::string& bucket, std::vector<uint8_t>& encryption_key) :
		basic_endpoint(endpoint_type::s3, std::string("s3://")+access_key+':'+secret_key+'@'+bucket),
		access_key_(access_key), secret_key_(secret_key), bucket_(bucket), encryption_key_(encryption_key)
	{
	}

	endpoint(const endpoint& t) :
		basic_endpoint(dynamic_cast<basic_endpoint&>(t)),
		access_key_(t.access_key_), secret_key_(t.secret_key_), bucket_(t.bucket_), encryption_key_(t.encryption_key_)
	{
	}

	virtual bool operator==(const endpoint& rhs) const {
		return type_ == rhs.type_ &&
				access_key_ == rhs.access_key_ &&
				secret_key_ == rhs.secret_key_ &&
				bucket_ == rhs.bucket_ &&
				encryption_key_ == rhs.encryption_key_
				;
	}

	virtual std::ostream& operator<<(std::ostream& os) {
		return os << "s3://**ak**:**sk**@"<<bucket_;
	}
};

}  // namespace s3


class endpoint_finder {
	mutable coreapi api_;
public:
	endpoint_finder (coreapi& api) : api_(api)
	{
	}

	endpoint::pointer operator()(const archiving::org_id& org_id) const {
		std::ostringstream ostr;
		ostr << "/archivingendpoints/" << org_id;

		// TODO use factory to make endpoints

		const std::string& response = api_.get(ostr.str());

		// FIXME run XPath & create definition;

		std::vector<uint8_t> enc_key(32);
		const std::string key_str("abcdefghijklmnopqrstuvwxyz0123456");
		memcpy(&enc_key[0], &key_str[0], 32);
		s3::endpoint::pointer p(new s3::endpoint("xxx", "yyy", "archiving", enc_key));

		return p;
	}
};

}  // namespace storage

}  // namespace app

#endif /* STORAGE_ENDPOINT_HPP_ */
