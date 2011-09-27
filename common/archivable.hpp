/*
 * archivable.hpp
 *
 *  Created on: 25 Jan 2011
 *      Author: dkorah
 */

#ifndef ARCHIVED_FILE_HPP_
#define ARCHIVED_FILE_HPP_

#include "md5.hpp"
#include "types.hpp"
#include "policy.hpp"

#include <rml/logger.h>
#include <rml/exception.h>
#include <rml/pc_channel.h>
#include <rml/utils.h>

#include <boost/shared_ptr.hpp>
#include <boost/foreach.hpp>

#include <iostream>
#include <vector>

#include <string.h>

namespace archiving {
class archivable;
}

namespace std {
inline ostream& operator<<(ostream& ostr, const archiving::archivable& a);
}

namespace archiving {

struct owner_info
{
	typedef boost::shared_ptr<owner_info> pointer;

	org_id_t orgID;
	std::vector< user_id_t > userIDs;

	owner_info() : orgID(0)
	{
	}

	// TODO how about users from other legs and if the call gets transferred around
	owner_info(const org_id_t& orgID, const std::vector< user_id_t >& userIDs = std::vector< user_id_t >())
	: orgID(orgID), userIDs(userIDs)
	{
	}

	/**
	 * addUser - add userID
	 *
	 * @param userID
	 */
	void addUser(const user_id_t& userID)
	{
		userIDs.push_back(userID);
	}
};

class archivable_pointer_compare;

class archivable
{
public:
	enum constants {
		save_retry_max_count = 3,
		save_retry_multiplier_seconds = 3
	};

	typedef boost::shared_ptr<archivable> pointer;

	friend class archivable_pointer_compare;
	friend std::ostream& std::operator<<(std::ostream& ostr, const archivable& a);

	struct state
	{
		enum state_t
		{
			unknown = 0,
			collected,
			inspected,
			stored,
			discarded,

			n_states
		};

		static const char* to_string(const state_t& s)
		{
			static const char* str[] = {
					"unknown",
					"collected",
					"inspected",
					"stored",
					"discarded"
			};

			if(s > 0 && s < n_states)
				return str[s];

			return str[0];
		}

		static state_t from_string(const std::string& str)
		{
			if(str == "unknown")
				return unknown;
			else if(str == "collected")
				return collected;
			else if(str == "inspected")
				return inspected;
			else if(str == "stored")
				return stored;
			else if(str == "discarded")
				return discarded;
			else
				THROW_RML_EXCEPTION("Unknown state from string: "<<str);
		}
	};

	/**
	 * Types of files we deal with
	 */
	struct file_type
	{
		enum file_type_t
		{
			unknown = 0,
			cdr,
			recording,
			sms,

			n_file_types
		};

		/**
		 * to string for of the type
		 *
		 * @param type
		 * @return
		 */
		static const char* to_string(const file_type_t type)
		{
			static const char* types_str[] = {
				"unknown",
				"cdr",
				"recording",
				"sms",

				"invalid-file-type"
			};

			if(type >= unknown && type < n_file_types)
				return types_str[type];

			return types_str[n_file_types];
		}

		/**
		 * from string to type
		 *
		 * @param str
		 * @return
		 */
		static const file_type_t from_string(const std::string& str)
		{
			if(str == "unknown")
				return unknown;
			else if(str == "cdr")
				return cdr;
			else if(str == "recording")
				return recording;
			else if(str == "sms")
				return sms;

			THROW_RML_EXCEPTION("from_string: invalid file_type:"<<str);
		}

		friend std::ostream& operator << (std::ostream& ostr, const file_type_t& type)
		{
			return ostr << file_type::to_string(type);
		}
	};

	struct identifier
	{
		file_type::file_type_t filetype;
		org_id_t org;
		std::string uuid;

		identifier() :
			filetype(file_type::unknown), org(0), uuid()
		{
		}

		identifier(const file_type::file_type_t& filetype, const org_id_t& org, const std::string& uuid) :
			filetype(filetype), org(org), uuid(uuid)
		{
		}

		friend inline std::ostream& operator<< (std::ostream& ostr, const identifier& identifier)
		{
			return ostr << identifier.filetype << '/' << identifier.org << '/' << identifier.uuid;
		}

		inline static std::string to_string(const identifier& identifier)
		{
			std::ostringstream ostr;
			ostr << identifier;

			return ostr.str();
		}

		inline operator std::string () const
		{
			return to_string(*this);
		}

		friend inline std::istream& operator>>(std::istream& istr, identifier& identifier)
		{
			char filetype_str[20];
			char org_str[20];
			char uuid_str[36];
			if(!(istr.getline(filetype_str, 20, '/') && istr.getline(org_str, 20, '/') && istr.getline(uuid_str, 36)))
				throw rml::exception("Invalid identifier string");

			identifier.filetype = file_type::from_string(filetype_str);
			identifier.org = rml::utils::strtoull(org_str);
			identifier.uuid = uuid_str;

			return istr;
		}
	};


protected:
	const std::string uuid;
	std::string originator_uuid;
	time_t start_epoch;
	std::string data_format;
	std::string pbx_hostname;
	bool add_to_cdrmunch_cache;

private:
	state::state_t state_;
	std::string file_path_;
	rml::utils::tmp_fstream::pointer p_fstream_;
	bool is_modified_;
	hash_vector md5_;

	const file_type::file_type_t type_;

	// do the parent only if all children have been done with
	unsigned int n_children_;
	unsigned int n_children_done_;

	pointer parent_;

	app::policy::id_type policy_id_;

	owner_info owners_;

	uint64_t database_id_;
	uint8_t retry;

	archivable();
	archivable(const archivable& a);

public:
//	archivable()
//	: start_epoch(0),
//	  data_format(0),
//	  pbx_hostname("unknown"),
//	  add_to_cdrmunch_cache(false),
//	  type_(file_type::unknown),
//	  n_children_(0),
//	  n_children_done_(0),
//	  parent_(0),
//	  policy_id_(0),
//	  database_id(0)
//	{
//	}

	/**
	 * Ctor ONLY for lookups etc
	 *
	 * @param uuid
	 * @param type
	 */
	archivable(const std::string& uuid, const file_type::file_type_t& type) :
		uuid(uuid),
		start_epoch(0),
		data_format("unknown"),
		pbx_hostname(),
		add_to_cdrmunch_cache(false),
		state_(state::unknown),
		file_path_(),
		p_fstream_(),
		is_modified_(false),
		type_(type),
		n_children_(0),
		n_children_done_(0),
		parent_(),
		policy_id_(0),
		database_id_(0),
		retry(0)
	{
		// Just for the comparisons and find to work
	}

	/**
	 * Constructor
	 *
	 * @param uuid
	 * @param file_path
	 * @param type
	 * @param parent
	 * @param policy_id
	 * @return
	 */
	archivable(
			const std::string& uuid,
			const std::string& originator_uuid,
			const time_t& start_epoch,
			const std::string& file_path,
			const file_type::file_type_t& type,
			const std::string& data_format,
			const std::string& pbx_hostname,
			const pointer& parent = pointer(),
			const app::policy::id_type& policy_id = app::policy::id_type(0),
			const bool& add_to_cdrmunch_cache = false,
			const bool& is_modified = false,
			const std::string& modified_file_path = "",
			const std::string& md5 = "",
			const state::state_t& state = state::collected
	)
	: uuid(uuid),
	  originator_uuid(originator_uuid),
	  start_epoch(start_epoch),
	  data_format(data_format),
	  pbx_hostname(pbx_hostname),
	  add_to_cdrmunch_cache(add_to_cdrmunch_cache),
	  state_(state),
	  file_path_(file_path),
	  p_fstream_(new rml::utils::tmp_fstream((is_modified && !modified_file_path.empty()? modified_file_path : file_path), std::fstream::in | std::fstream::out | std::fstream::binary)),
	  is_modified_(is_modified),
	  type_(type),
	  n_children_(0),
	  n_children_done_(0),
	  parent_(parent),
	  policy_id_(policy_id),
	  database_id_(0),
	  retry(0)
	{
		if(md5.empty()) {
			md5_ = md5hasher::hash(p_fstream_);
		}
		else {
			std::istringstream istr(md5);
			istr >> md5_;
		}

		// set persistent; we will get around to making it NOT persistent when we are done with it
		p_fstream_->set_persistent(true);

		// close it, re_open when needed; file handles are very precious
		p_fstream_->close();
	}

	void set_originator_uuid(const std::string& new_originator_uuid)
	{
		originator_uuid = new_originator_uuid;
	}

	std::string get_originator_uuid()
	{
		return originator_uuid;
	}

	void set_start_epoch(const time_t& epoch)
	{
		start_epoch = epoch;
	}

	inline std::ostream& shove_metadata(std::ostream& ostr) const
	{
		// if this NOT a CDR usual ownership applies
		if(type_ != file_type::cdr) {
			ostr << "OrgID: "<<owners_.orgID<<"\r\n";
			if(!owners_.userIDs.empty()) {
				ostr << "UserIDs: ";
				std::vector< archiving::user_id_t >::const_iterator end(owners_.userIDs.end());
				bool first = true;
				for(std::vector< archiving::user_id_t >::const_iterator itr = owners_.userIDs.begin(); itr != end; ++itr) {
					if(!first)
						ostr << ',';
					ostr << *itr;
					first = false;
				}
				ostr << "\r\n";
			}
		}
		// Otherwise i.e if CDR; Org 1 owns it all
		else {
			ostr << "OrgID: "<<org_id_t(1)<<"\r\n";
			ostr << "CallOwnerOrg: "<<owners_.orgID<<"\r\n"; // custom header
			if(!owners_.userIDs.empty()) {
				ostr << "CallOwnerUserIDs: "; // custom header
				std::vector< archiving::user_id_t >::const_iterator end(owners_.userIDs.end());
				bool first = true;
				for(std::vector< archiving::user_id_t >::const_iterator itr = owners_.userIDs.begin(); itr != end; ++itr) {
					if(!first)
						ostr << ',';
					ostr << *itr;
					first = false;
				}
				ostr << "\r\n";
			}
		}

		std::string leg_uuid;
		{
			if(parent_){
				if(parent_->type_ != file_type::cdr)
					LogError(*this<<"Parent arcivable NOT a CDR ?!?");

				leg_uuid = parent_->uuid;
			}
			else
				leg_uuid = uuid;
		}

		// TODO Get rid of OriginatorUUID

		return ostr << "FileType: "<<type_<<"\r\n"
				<< "MD5: "<<md5_<<"\r\n"
				<< "CreateTime: "<<start_epoch<<"\r\n"
				<< "RetentionPolicyID: "<<policy_id_<<"\r\n"
				<< "LegUUID: "<<leg_uuid<<"\r\n"
				<< "OriginatorUUID: "<<originator_uuid<<"\r\n"
				<< "DataFormat: "<<data_format<<"\r\n"
				<< "PBXHostName: "<<pbx_hostname<<"\r\n"
				<< "AddToCDRMunchCache: "<<(add_to_cdrmunch_cache? "true": "false")<<"\r\n"
				;
	}

	inline std::ostream& shove_put_request(std::ostream& ostr) const
	{
#ifndef RML_NO_DEBUG
		std::string put_request;
		{
			std::ostringstream ostr;
#endif
		ostr << "PUT "<<type_<<' '<<((type_ != file_type::cdr)? owners_.orgID: org_id_t(1))<<' '<<uuid<<' '<<p_fstream_->get_size()<<"\r\n";
		shove_metadata(ostr);
#ifndef RML_NO_DEBUG
			put_request = ostr.str();
		}
		LogDebug("ToArchiveD: "<<put_request);
		ostr << put_request;
#endif
		return ostr << "\r\n";
	}

	inline rml::utils::tmp_fstream::pointer get_fstream_ptr() const
	{
		return p_fstream_;
	}

	inline const hash_vector& get_md5() const
	{
		return md5_;
	}

	inline const file_type::file_type_t& get_type() const
	{
		return type_;
	}

	inline const std::string get_uuid() const
	{
		return uuid;
	}

	inline const time_t get_start_epoch() const
	{
		return start_epoch;
	}

	inline void set_add_to_cdrmunch_cache(const bool& add_it = true)
	{
		add_to_cdrmunch_cache = add_it;
	}

	inline bool get_add_to_cdrmunch_cache() const
	{
		return add_to_cdrmunch_cache;
	}

	inline std::string get_data_format() const
	{
		return data_format;
	}

	inline pointer get_parent() const
	{
		return parent_;
	}

	inline uint64_t get_database_id() const
	{
		return database_id_;
	}

	inline void set_database_id(const uint64_t& id)
	{
		database_id_ = id;
	}

	inline uint8_t get_retry() const
	{
		return retry;
	}

	inline state::state_t get_current_state() const
	{
		return state_;
	}

	inline void set_current_state(const state::state_t& state)
	{
		state_ = state;
	}

	inline std::string get_hostname() const
	{
		return pbx_hostname;
	}

	inline void set_hostname(const std::string &hostname)
	{
		pbx_hostname = hostname;
	}

	inline app::policy::id_type get_policy_id() const
	{
		return policy_id_;
	}

	inline void set_policy_id(app::policy::id_type id)
	{
		policy_id_ = id;
	}

	void set_owners(const owner_info& o)
	{
		owners_.orgID = o.orgID;
		owners_.userIDs.clear();
		user_id_t userID;
		BOOST_FOREACH(userID, o.userIDs) {
			owners_.addUser(userID);
		}
	}

	std::vector< user_id_t > get_owners() const
	{
		return owners_.userIDs;
	}

	/**
	 * Get Org
	 *
	 * @return
	 */
	inline const org_id_t& get_org_id() const
	{
		return owners_.orgID;
	}

	inline std::pair< std::string, std::string > get_paths() const
	{
		if(is_modified_)
			return std::make_pair<std::string, std::string>(file_path_, p_fstream_->get_filename());

		return std::make_pair<std::string, std::string>(file_path_, "");
	}

	/**
	 * Set a new intermediate file that replaces the archivable's fstream
	 *
	 * @param tmp_fstr
	 * @param do_close_old
	 */
	void set_fstream(const rml::utils::tmp_fstream::pointer& tmp_fstr, const bool& do_close_old = true)
	{
		if( 0 == tmp_fstr->get_size() )
			THROW_RML_EXCEPTION("Zero length file; path:"<<tmp_fstr->get_filename());

		if(do_close_old && p_fstream_)
			p_fstream_->close();

		if(is_modified_) {
			LogMessage(*this << " Already modified");
			p_fstream_->set_persistent(false);
		}

		is_modified_ = true;

		p_fstream_ = tmp_fstr;
		md5_ = md5hasher::hash(tmp_fstr);
	}

	/**
	 * Mark an archivable article to be done with
	 * Leads to unlink() of the file
	 */
	void set_done(const bool discarded)
	{
		if(p_fstream_) {
			if(p_fstream_->is_open())
				p_fstream_->close();

			p_fstream_->set_persistent(false);
		}

		state_ = discarded? state::discarded: state::stored;

		if(is_modified_) {
			if( 0 != chmod(file_path_.c_str(), 0666) )
				LogWarning(*this<<' '<<file_path_ << " chmod for unlink failed - err:" << strerror(errno));

			if( 0 != unlink(file_path_.c_str()) )
				LogWarning(*this<<' '<<file_path_ << " unlink failed - err:" << strerror(errno));
			else
				LogVerbose(*this<<' '<<file_path_ << " unlinked");
		}

		LogMessage(*this<<" done");
	}
};

/**
 * comparator for channel
 */
class archivable_pointer_compare
{
public:
	bool operator() (const archivable::pointer& lhs, const archivable::pointer& rhs) const
	{
		if (lhs->uuid == rhs->uuid)
			return lhs->get_type() < rhs->get_type();

		return lhs->uuid < rhs->uuid;
	}
};

typedef rml::pc_channel<archivable::pointer, archivable_pointer_compare> archivable_channel_t;
typedef rml::busy_set<archivable::pointer, archivable_pointer_compare> archivable_busy_set_t;

typedef rml::busy_set< archivable::pointer, archivable_pointer_compare > archivable_set_t;

}  // namespace archiving

namespace std {

inline ostream& operator<<(ostream& ostr, const archiving::archivable& a)
{
	return ostr << a.get_type() << '/' << a.get_org_id() << '/' << a.uuid << " state:"<<archiving::archivable::state::to_string(a.get_current_state());
}

inline ostream& operator<<(ostream& ostr, const archiving::archivable::pointer& p_a)
{
	if(p_a)
		return ostr << *p_a;

	return ostr << "archivable::pointer(NULL)";
}

inline istream& operator>>(istream& istr, archiving::archivable::file_type::file_type_t& t)
{
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
