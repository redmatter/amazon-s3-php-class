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

#include <libaws/aws.h>

#include <rml/exception.h>

#include "storage_operation.hpp"

namespace app {

namespace storage {

template < class Backend >
class basic_backend {

	typedef Backend::object_identifier_t object_identifier_t;

	static Backend* p_backend_;

public:
	typedef boost::function<void (const bool, const object_identifier_t&, boost::shared_ptr<std::fstream>)> fn_get_handler_t;

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

	virtual	boost::shared_ptr<std::fstream> open(const object_identifier_t&, const operation::operation_t&, const std::iostream::openmode&);

};

class s3_backend : public basic_backend<s3_backend> {

	const boost::program_options::variables_map& confMap_;

	aws::ConnectionPool<aws::S3ConnectionPtr>* s3ConnectionPool_;

protected:
	s3_backend(const boost::program_options::variables_map& confMap) :
		confMap_(confMap), s3ConnectionPool_(0)
	{
		s3ConnectionPool_ = new aws::ConnectionPool<aws::S3ConnectionPtr>(
				confMap_["s3_backend.pool_size"].as<unsigned int>(),
				!confMap_["s3_backend.access_key"].as<std::string>().empty()?
						confMap_["s3_backend.access_key"].as<std::string>(): throw rml::exception("s3_backend.access_key not configured"),
				!confMap_["s3_backend.secret_key"].as<std::string>().empty()?
						confMap_["s3_backend.secret_key"].as<std::string>(): throw rml::exception("s3_backend.secret_key not configured")
				);
	}

	~s3_backend()
	{
	}

public:
	friend class basic_backend<s3_backend>;

	class object_identifier {
		std::pair<std::string, std::string> key;
		std::size_t content_length;
		boost::shared_ptr<rml::utils::tmp_fstream> fstream;
	};

	static void describe_config(boost::program_options::options_description& optsDesc) {
		optsDesc.add_options()
				("s3_backend.access_key", boost::program_options::value<std::string>()->default_value(""), "S3 access key")
				("s3_backend.secret_key", boost::program_options::value<std::string>()->default_value(""), "S3 secret key")
				("s3_backend.pool_size",  boost::program_options::value<unsigned int>()->default_value(5), "S3 connection pool size")
				;
	}

	boost::shared_ptr<std::fstream> open(const object_identifier& obj, const operation::operation_t& op, const std::iostream::openmode& mode)
	{
		switch(op) {

		case operatation::get_object:
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

				obj.fstream.reset(new rml::utils::tmp_fstream(obj.key.first+'-'+obj.key.second, p_getRes->getContentLength()));
				*obj.fstream << p_getRes->getInputStream().rdbuf();

				return boost::static_pointer_cast< std::fstream, rml::utils::tmp_fstream >(obj.fstream);
			}
			catch(const aws::GetException& e) {
				THROW_RML_EXCEPTION(obj<<" - GET failed - error:"<<e.what());
			}
			break;
		}

		case operation::put_object_init:
		{
			obj.fstream.reset(new rml::utils::tmp_fstream(obj.key.first+'-'+obj.key.second, obj.content_length));
			return boost::static_pointer_cast< std::fstream, rml::utils::tmp_fstream >(obj.fstream);
			break;
		}

		default:
			THROW_RML_EXCEPTION(obj<<" - Invalid operation - operation:"<<op);
			break;

		}

		return boost::shared_ptr<std::fstream>();
	}

	bool commit_put(const object_identifier& obj)
	{
		try {
			if(!obj.fstream.get() || !obj.fstream->is_open())
				throw rml::exception("operation::put_object - object_identifier has NO associated stream");

			std::ifstream* ifs = static_cast<std::ifstream*>(obj.fstream.get());

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

}  // namespace storage

}  // namespace app

#endif /* BASIC_STORAGE_BACKEND_HPP_ */
