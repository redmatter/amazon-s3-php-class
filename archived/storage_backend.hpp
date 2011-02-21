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

#include "storage_operation.hpp"
#include "storage_object_identifier.hpp"

namespace app {

namespace storage {


template < typename Backend, typename object_identifier_t >
class basic_backend : public boost::noncopyable {
	static Backend* p_backend_;

public:
	static void destroy() {
		static bool destroyed = false;
		if(!destroyed) {
			delete p_backend_;
			p_backend_ = 0;
		}
	}

	static Backend* get_instance(const boost::program_options::variables_map& confMap = boost::program_options::variables_map()) {
		if(!p_backend_) {
			assert(!confMap.empty());
			p_backend_ = new Backend(confMap);
			std::atexit(destroy);
		}

		return p_backend_;
	}

	virtual	boost::shared_ptr<std::fstream> open(object_identifier_t&, const operation::operation_t&, const std::iostream::openmode&) = 0;

	virtual ~basic_backend() {};

};

template <typename Backend, typename object_identifier_t>
Backend* basic_backend< Backend, object_identifier_t >::p_backend_(0);

// specialisation

namespace s3 {

struct endpoint_description {
	std::string access_key;
	std::string secret_key;

	typedef boost::shared_ptr<endpoint_description> pointer;

	inline bool operator==(const endpoint_description& rhs) const {
		return access_key == rhs.access_key && secret_key == rhs.secret_key;
	}
};

class backend : public basic_backend<backend, object_identifier> {

	const boost::program_options::variables_map& confMap_;

	aws::ConnectionPool<aws::S3ConnectionPtr>* s3ConnectionPool_;

protected:
	backend(const boost::program_options::variables_map& confMap) :
		confMap_(confMap),
		s3ConnectionPool_(
				new aws::ConnectionPool<aws::S3ConnectionPtr>(
						confMap_["s3_backend.pool_size"].as<unsigned int>(),
						!confMap_["s3_backend.access_key"].as<std::string>().empty()?
								confMap_["s3_backend.access_key"].as<std::string>() :
								throw rml::exception("s3_backend.access_key not configured"),
						!confMap_["s3_backend.secret_key"].as<std::string>().empty()?
								confMap_["s3_backend.secret_key"].as<std::string>() :
								throw rml::exception("s3_backend.secret_key not configured")))
	{
		LogMessage("S3 backend created");
	}

	virtual ~backend()
	{
		LogMessage("S3 backend destroyed");
	}

public:

	friend class basic_backend<backend, object_identifier>;

	static void describe_config(boost::program_options::options_description& optsDesc) {
		optsDesc.add_options()
				("s3_backend.access_key", boost::program_options::value<std::string>()->default_value(""), "S3 access key")
				("s3_backend.secret_key", boost::program_options::value<std::string>()->default_value(""), "S3 secret key")
				("s3_backend.pool_size",  boost::program_options::value<unsigned int>()->default_value(5), "S3 connection pool size")
				;
	}

	boost::shared_ptr<std::fstream> open(object_identifier& obj, const operation::operation_t& op, const std::iostream::openmode& mode)
	{
		switch(op) {

		case operation::get_object:
		{
			try {
				aws::GetResponsePtr p_getRes = s3ConnectionPool_
						->getConnection()
						->get(obj.key.first, obj.key.second);

// not to reset as it might be used by another thread; stream gets closed on delete anyway!
//				if(obj.fstream) {
//					if(obj.fstream->is_open())
//						obj.fstream->close();
//					obj.fstream.reset();
//				}

				rml::utils::tmp_fstream::pointer fstr(new rml::utils::tmp_fstream(obj.key.first+'-'+obj.key.second, p_getRes->getContentLength()));
				*fstr << p_getRes->getInputStream().rdbuf();

				const std::map<std::string, std::string>& metadata(p_getRes->getMetaData());
				std::map<std::string, std::string>::const_iterator itr = metadata.find("rm-file-type");
				if(itr != metadata.end()) {
					obj.type = object_type::from_sring(itr->second);
					if(obj.type == object_type::unknown) {
						LogWarning("Unknown object_type "<<itr->second<<"; assuming 'clear'");
						obj.type = object_type::clear;
					}
				}
				else {
					LogWarning("Object type not set on "<<obj<<"; assuming 'clear'");
					obj.type = object_type::clear;
				}

				obj.fstream = boost::static_pointer_cast< std::fstream, rml::utils::tmp_fstream >(fstr);
				obj.content_length = p_getRes->getContentLength();

				return obj.fstream;
			}
			catch(const aws::GetException& e) {
				THROW_RML_EXCEPTION(obj<<" - GET failed - error:"<<e.what());
			}
			break;
		}

		case operation::put_object_init:
		{
			rml::utils::tmp_fstream::pointer fstr(new rml::utils::tmp_fstream(obj.key.first+'-'+obj.key.second, obj.content_length));
			obj.fstream = boost::static_pointer_cast< std::fstream, rml::utils::tmp_fstream >(fstr);

			return obj.fstream;
			break;
		}

		default:
			THROW_RML_EXCEPTION(obj<<" - Invalid operation - operation:"<<op);
			break;

		}

		return boost::shared_ptr<std::fstream>();
	}

	bool commit_put(object_identifier& obj)
	{
		try {
			if(!obj.fstream.get() || !obj.fstream->is_open())
				throw rml::exception("operation::put_object - object_identifier has NO associated stream");

			std::ifstream* ifs = reinterpret_cast<std::ifstream*>(obj.fstream.get());

			aws::PutResponsePtr p_putRes = s3ConnectionPool_
					->getConnection()
					->put(obj.key.first, obj.key.second, *ifs, "application/octet-stream", 0, (long)obj.content_length);

			return true;
		}
		catch(const aws::GetException& e) {
			THROW_RML_EXCEPTION(obj<<" - PUT failed - error:"<<e.what());
		}

		return false;
	}
};

}  // namespace s3

}  // namespace storage

}  // namespace app

#endif /* BASIC_STORAGE_BACKEND_HPP_ */
