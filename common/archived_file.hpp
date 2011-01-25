/*
 * archived_file.hpp
 *
 *  Created on: 25 Jan 2011
 *      Author: dkorah
 */

#ifndef ARCHIVED_FILE_HPP_
#define ARCHIVED_FILE_HPP_

#include <string.h>

#include <boost/shared_ptr.hpp>

#include <rml/logger.h>

#include "md5.hpp"

namespace archiving {

class archived_file {
	bool error;
	std::string error_str;

	std::string file_path;

public:
	typedef boost::shared_ptr<std::ifstream> ifstream_ptr;
	typedef boost::shared_ptr<archived_file> pointer;

	typedef std::map<std::string, std::string> policy;
	typedef boost::shared_ptr<policy> policy_ptr;

	struct state {
		enum {
			collected,
			stored,
			stored_acked,
		};
	};

private:
	ifstream_ptr ifs;
	hash_vector md5;

public:
	archived_file(const std::string& file_path, const policy_ptr& policy)
	: error(false),
	  file_path(file_path),
	  ifs(new std::ifstream(file_path.c_str(), std::ios::binary))
	{
		if(!*(*ifs))
			throw rml::exception("failed to open file: "<<file_path<<" err: "<<strerror(errno));

		ifs->exceptions(std::ios::badbit | std::ios::failbit);

		md5 = md5hasher::hash(*(*ifs));
	}

	ifstream_ptr get_stream() {
		return ifs;
	}

	const hash_vector& get_md5() const {
		return md5;
	}

	const std::string& get_file_path() const {
		return file_path;
	}
};

}  // namespace archiving


#endif /* ARCHIVED_FILE_HPP_ */
