/*
 * archivable.hpp
 *
 *  Created on: 25 Jan 2011
 *      Author: dkorah
 */

#ifndef ARCHIVED_FILE_HPP_
#define ARCHIVED_FILE_HPP_

#include <string.h>

#include <iostream>

#include <boost/shared_ptr.hpp>

#include <rml/logger.h>
#include <rml/exception.h>

#include "md5.hpp"

namespace archiving {
class archivable;
}  // namespace archiving

namespace std {
ostream& operator<<(ostream& ostr, const archiving::archivable& a);
} // namespace std

namespace archiving {

typedef unsigned int org_id;
typedef int user_id;

struct owner_info {
	org_id orgID;
	std::vector< user_id > userIDs;

	owner_info() {}

	// TODO how about users from other legs and if the call gets transferred around
	owner_info(const org_id& orgID, const std::vector< user_id >& userIDs = std::vector< user_id >())
	: orgID(orgID), userIDs(userIDs)
	{ }

	/**
	 * addUser - add userID
	 *
	 * @param userID
	 */
	void addUser(const user_id& userID) {
		userIDs.push_back(userID);
	}
};

class archivable_pointer_compare;

class archivable {
	bool error;
	std::string error_str;

protected:
	std::string file_path;
	bool deletable;

public:
	typedef boost::shared_ptr<std::ifstream> ifstream_ptr;
	typedef boost::shared_ptr<archivable> pointer;

	typedef boost::shared_ptr<rml::utils::tmp_file> tmp_file_ptr;

	friend class archivable_pointer_compare;

	typedef std::map<std::string, std::string> policy;
	typedef boost::shared_ptr<policy> policy_ptr;

	struct state {
		enum {
			collected,
			stored,
			stored_acked,
		};
	};

	struct file_type {
		enum type {
			unknown = 0,
			cdr,
			call_recording,
			hold_music,
			voice_recording,

			n_file_types
		};

		static const char* to_string(const type t) {
			static const char* types_str[] = {
					"unknown",
					"cdr",
					"call_recording",
					"hold_music",
					"voice_recording",

					"invalid-file-type"
			};

			if(t >= unknown && t < n_file_types)
				return types_str[t];

			return types_str[n_file_types];
		}
	};

private:
	ifstream_ptr ifs;
	hash_vector md5;
	std::vector<tmp_file_ptr> tmp_files;

	size_t file_size;

	const file_type::type type;

	pointer parent;

	owner_info owners;

public:
	friend std::ostream& std::operator<<(std::ostream& ostr, const archivable& a);

	archivable(const std::string& file_path, const file_type::type& type, const pointer& parent = pointer(), const policy_ptr& policy = policy_ptr())
	: error(false),
	  file_path(file_path),
	  deletable(false),
	  type(type),
	  parent(parent)
	{
		set_file_path(file_path);
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

	const file_type::type& get_type() const {
		return type;
	}

	ifstream_ptr get_file_ifstream_ptr() {
		return ifs;
	}

	void set_owners(const owner_info& o) {
		owners.orgID = o.orgID;
		owners.userIDs.clear();
		BOOST_FOREACH(user_id userID, o.userIDs) {
			owners.addUser(userID);
		}
	}

	const org_id& get_org_id() const {
		return owners.orgID;
	}

	void set_deletable() {
		deletable = true;
	}

	const bool is_deletable() const {
		return deletable;
	}

	const size_t& get_file_size() const {
		return file_size;
	}

	void set_file_path(const std::string& path, const tmp_file_ptr& tmp_file = tmp_file_ptr()) {

		if(tmp_file.get()) {
			tmp_files.push_back(tmp_file);
			assert(tmp_file->getName() == path);
		}

		if(ifs)
			ifs->close();

		file_path = path;
		ifs.reset(new std::ifstream(file_path.c_str(), std::ios::binary));

		if(ifs->fail())
			THROW_RML_EXCEPTION("failed to open file: "<<file_path<<" err: "<<strerror(errno));

		ifs->exceptions(std::ios::badbit | std::ios::failbit);

		file_size = 0;
		{
			std::filebuf *pbuf = ifs->rdbuf();
			file_size = pbuf->pubseekoff(0,std::ios::end, std::ios::in);
			pbuf->pubseekpos(0,std::ios::in);
		}

		if( 0 == file_size )
			throw rml::exception("Zero length CDR");

		md5 = md5hasher::hash(*ifs.get());
	}
};

class archivable_pointer_compare {
public:
	bool operator() (const archivable::pointer& lhs, const archivable::pointer& rhs) {
		return lhs->file_path < rhs->file_path;
	}
};


}  // namespace archiving

namespace std {

ostream& operator<<(ostream& ostr, const archiving::archivable& a) {
	return ostr << archiving::archivable::file_type::to_string(a.get_type()) << ": " << a.get_file_path() << " md5:" << a.get_md5();
}

}  // namespace std

#endif /* ARCHIVED_FILE_HPP_ */
