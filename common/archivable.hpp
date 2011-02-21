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
#include <rml/pc_channel.h>

#include "md5.hpp"

namespace archiving {

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
protected:
	std::string file_path;
	bool deletable;

public:
	typedef boost::shared_ptr<std::fstream> fstream_ptr;
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
		enum file_type_t {
			unknown = 0,
			cdr,
			call_recording,
			hold_music,
			voice_recording,

			n_file_types
		};

		static const char* to_string(const file_type_t t) {
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


		static const file_type_t from_string(const std::string& str) {
			file_type_t type;
			if(str == "unknown")
				type = unknown;
			else if(str == "cdr")
				type = cdr;
			else if(str == "call_recording")
				type = call_recording;
			else if(str == "hold_music")
				type = hold_music;
			else if(str == "voice_recording")
				type = voice_recording;
			else
				THROW_RML_EXCEPTION("from_string: invalid file_type:"<<str);

			return type;
		}
	};

private:
	fstream_ptr fstr_;
	hash_vector md5_;

	std::vector<rml::utils::tmp_fstream::pointer> tmp_fstreams_;

	size_t file_size_;

	const file_type::file_type_t type_;

	pointer parent_;

	owner_info owners_;

public:
	archivable(const std::string& file_path, const file_type::file_type_t& type, const pointer& parent = pointer(), const policy_ptr& policy = policy_ptr())
	: file_path(file_path),
	  deletable(false),
	  type_(type),
	  parent_(parent)
	{
		set_file_path(file_path);
	}

	fstream_ptr get_stream() {
		return fstr_;
	}

	const hash_vector& get_md5() const {
		return md5_;
	}

	const std::string& get_file_path() const {
		return file_path;
	}

	const file_type::file_type_t& get_type() const {
		return type_;
	}

	fstream_ptr get_file_ifstream_ptr() {
		return fstr_;
	}

	void set_owners(const owner_info& o) {
		owners_.orgID = o.orgID;
		owners_.userIDs.clear();
		BOOST_FOREACH(user_id userID, o.userIDs) {
			owners_.addUser(userID);
		}
	}

	const org_id& get_org_id() const {
		return owners_.orgID;
	}

	void set_deletable() {
		deletable = true;
	}

	const bool is_deletable() const {
		return deletable;
	}

	const size_t& get_file_size() const {
		return file_size_;
	}

	void set_file_path(const std::string& path, const rml::utils::tmp_fstream::pointer& tmp_fstr = rml::utils::tmp_fstream::pointer()) {
		if(tmp_fstr) {
			tmp_fstreams_.push_back(tmp_fstr);
			assert(tmp_fstr->get_filename() == path);

			fstr_ = tmp_fstr;
		}
		else {
			fstr_.reset(new std::fstream(file_path.c_str(), std::ios_base::in | std::ios_base::binary));
			if(fstr_->fail())
				THROW_RML_EXCEPTION("failed to open file: "<<file_path<<" err: "<<strerror(errno));

			fstr_->exceptions(std::ios::badbit | std::ios::failbit);
		}

		if(file_path.empty())
			file_path = path;

		file_size_ = 0;
		{
			std::filebuf *pbuf = fstr_->rdbuf();
			file_size_ = pbuf->pubseekoff(0,std::ios::end, std::ios::in);
			pbuf->pubseekpos(0,std::ios::in);
		}

		if( 0 == file_size_ )
			throw rml::exception("Zero length CDR");

		md5_ = md5hasher::hash(fstr_, file_size_);
	}
};

class archivable_pointer_compare {
public:
	bool operator() (const archivable::pointer& lhs, const archivable::pointer& rhs) {
		return lhs->file_path < rhs->file_path;
	}
};

typedef rml::pc_channel<archivable::pointer, archivable_pointer_compare> archivable_channel_t;

}  // namespace archiving

namespace std {

inline ostream& operator<<(ostream& ostr, const archiving::archivable& a) {
	return ostr << archiving::archivable::file_type::to_string(a.get_type()) << ": " << a.get_file_path() << " md5:" << a.get_md5();
}

inline ostream& operator<<(ostream& ostr, const archiving::archivable::file_type::file_type_t& t) {
	return ostr << archiving::archivable::file_type::to_string(t);
}

inline istream& operator>>(istream& istr, archiving::archivable::file_type::file_type_t& t) {
	std::string tmp; istr >> tmp;
	try {
		t = archiving::archivable::file_type::from_string(tmp); // we may throw here
	}
	catch(...) {
		LogWarning("Invalid file_type");
	}
	return istr;
}

}  // namespace std

#endif /* ARCHIVED_FILE_HPP_ */
