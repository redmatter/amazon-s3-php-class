/*
 * types.hpp
 *
 *  Created on: 02 Feb 2011
 *      Author: dkorah
 */

#ifndef APP_TYPES_HPP_
#define APP_TYPES_HPP_

#include "globals.h"

#include "../common/defines.h"
#include "../common/types.hpp"
#include "../common/policy.hpp"

#include <rml/logger.h>
#include <rml/utils.h>

#include <functional>
#include <algorithm>
#include <iomanip>
#include <map>
#include <set>

#include <stdint.h>
#include <string.h>

/*
+-------------------------------------------+
|   ["RMA-CE"]                    6 bytes   |
|   [-archived-version-]          3 bytes   |
|   [-file-format-version-]       2 bytes   |
|   [-IV-]                        8 bytes   |
|   [-padding-]                  21 bytes   |
+---------------- encrypted ----------------+
| +---------------------------------------+ |
| | ["RMA"]                       3 bytes | |
| | [-format-]                    1 bytes | |  <- format specification below
| | [-sub-format-]                1 bytes | |
| | [-compressed-content-length-] 8 bytes | |
| | [-clear-content-length-]      8 bytes | |
| | [-CRC32-]                     4 bytes | |
| | [-padding-]                   7 bytes | |
| +---------------------------------------+ |
| | **   -compressed-or-clear-data-    ** | |
| +---------------------------------------+ |
| | [-8-byte-block-size-pad-]     x bytes | |
| +---------------------------------------+ |
+-------------------------------------------+


format specification
* 0x00 clear
* 0x01 compressed
* 0x10 encrypted           <- only for internal use

sub format specification
* 0x00 unknown OR error
* 0x01 application/octet-stream     OR blob
* 0x02 application/xml              OR xml
* 0x03 audio/x-wav                  OR wav
* 0x04 audio/mpeg                   OR mp3
* 0x05 application/vnd.tcpdump.pcap OR pcap
*/

namespace app {

namespace types {

struct file_format_version
{
	enum {
		major = 1,
		minor = 0
	};
};

struct data_format
{
	enum data_format_t
	{
		clear      = 0x0000,
		compressed = 0x0001,
		encrypted  = 0x0010
	};
};

struct data_format_mask
{
	enum data_format_mask_t
	{
		compressed = 0x000f,
		encrypted  = 0x00f0
	};
};

struct data_sub_format
{
	enum data_sub_format_t
	{
		unknown   = 0,    // flags errors
		binary, //= 0x01, // blob ( catchall for any unrecognized data )
		xml,    //= 0x02,
		wav,    //= 0x03,
		mp3,    //= 0x04,
		pcap,   //= 0x05,
		email,  //= 0x06,
		json,   //= 0x07,

		n_data_sub_format
	};

	static inline_ const char* to_string(const data_sub_format_t& data_sub_format)
	{
		static const char* str[] = {
				"unknown",
				"binary",
				"xml",
				"wav",
				"mp3",
				"pcap",
				"email",
				"json"
		};

		if(data_sub_format > unknown && data_sub_format < n_data_sub_format)
			return str[data_sub_format];

		return str[0];
	}

	static inline_ data_sub_format_t from_string(const std::string& str)
	{
		if(str == "binary")
			return binary;
		if(str == "xml")
			return xml;
		if(str == "wav")
			return wav;
		if(str == "mp3")
			return mp3;
		if(str == "pcap")
			return pcap;
		if(str == "email")
			return email;
		if(str == "json")
			return json;

		return unknown;
	}

	static inline data_sub_format_t from_mime_type(const std::string& mime)
	{
		if(mime == "application/octet-stream")
			return binary;
		if(mime == "application/xml")
			return xml;
		if(mime == "audio/wav")
			return wav;
		if(mime == "audio/mpeg")
			return mp3;
		if(mime == "application/vnd.tcpdump.pcap")
			return pcap;
		if(mime == "message/rfc822")
			return email;
		if(mime == "application/json")
			return json;

		return unknown;
	}

	static inline_ data_sub_format_t from_filename(const std::string& filename)
	{
		std::size_t pos = filename.rfind('.');
		data_sub_format_t ret = unknown;

		if(pos != std::string::npos)
			ret = from_string(std::string(filename, pos + 1));

		if(ret == unknown) {
			LogWarning("Unknown filetype from filename "<<filename);
			return binary;
		}

		return ret;
	}
};

/// file format states based on data_format & data_sub_format
/**
 *          clear   compressed   encrypted
 * binary     x          -           -       clear
 * xml        x          -           -       xml
 * wav        x          -           -       wav
 * mp3        x          -           -       mp3
 *
 * binary     -          x           -       compressed_binary
 * xml        -          x           -       compressed_xml
 * wav        -          x           -       compressed_wav
 *
 * binary     -          x           x       encrypted_compressed_binary
 * xml        -          x           x       encrypted_compressed_xml
 * wav        -          x           x       encrypted_compressed_wav
 *
 * mp3        -          -           x       encrypted_mp3
 *
 */

struct file_format
{
	enum file_format_t
	{
		unknown                      = 0,

		clear_binary                 = ( data_format::clear << 8 ) | data_sub_format::binary,
		clear_xml                    = ( data_format::clear << 8 ) | data_sub_format::xml,
		clear_wav                    = ( data_format::clear << 8 ) | data_sub_format::wav,
		clear_mp3                    = ( data_format::clear << 8 ) | data_sub_format::mp3,
		clear_pcap                   = ( data_format::clear << 8 ) | data_sub_format::pcap,
		clear_email                  = ( data_format::clear << 8 ) | data_sub_format::email,
		clear_json                   = ( data_format::clear << 8 ) | data_sub_format::json,

		compressed_binary            = ( data_format::compressed << 8 ) | data_sub_format::binary,
		compressed_xml               = ( data_format::compressed << 8 ) | data_sub_format::xml,
		compressed_wav               = ( data_format::compressed << 8 ) | data_sub_format::wav,
		compressed_pcap              = ( data_format::compressed << 8 ) | data_sub_format::pcap,
		compressed_email             = ( data_format::compressed << 8 ) | data_sub_format::email,
		compressed_json              = ( data_format::compressed << 8 ) | data_sub_format::json,

		encrypted_compressed_binary  = ((data_format::encrypted | data_format::compressed ) << 8 ) | data_sub_format::binary,
		encrypted_compressed_xml     = ((data_format::encrypted | data_format::compressed ) << 8 ) | data_sub_format::xml,
		encrypted_compressed_wav     = ((data_format::encrypted | data_format::compressed ) << 8 ) | data_sub_format::wav,
		encrypted_compressed_pcap    = ((data_format::encrypted | data_format::compressed ) << 8 ) | data_sub_format::pcap,
		encrypted_compressed_email   = ((data_format::encrypted | data_format::compressed ) << 8 ) | data_sub_format::email,
		encrypted_compressed_json    = ((data_format::encrypted | data_format::compressed ) << 8 ) | data_sub_format::json,

		encrypted_mp3                = ( data_format::encrypted << 8 ) | data_sub_format::mp3
	};

	static const char* to_string(const file_format_t& ff)
	{
		switch(ff) {
		case clear_binary:
			return "clear_binary";
		case clear_xml:
			return "clear_xml";
		case clear_wav:
			return "clear_wav";
		case clear_mp3:
			return "clear_mp3";
		case clear_pcap:
			return "clear_pcap";
		case clear_email:
			return "clear_email";
		case clear_json:
			return "clear_json";

		case compressed_binary:
			return "compressed_binary";
		case compressed_xml:
			return "compressed_xml";
		case compressed_wav:
			return "compressed_wav";
		case compressed_pcap:
			return "compressed_pcap";
		case compressed_email:
			return "compressed_email";
		case compressed_json:
			return "compressed_json";

		case encrypted_compressed_binary:
			return "encrypted_compressed_binary";
		case encrypted_compressed_xml:
			return "encrypted_compressed_xml";
		case encrypted_compressed_wav:
			return "encrypted_compressed_wav";
		case encrypted_compressed_pcap:
			return "encrypted_compressed_pcap";
		case encrypted_compressed_email:
			return "encrypted_compressed_email";
		case encrypted_compressed_json:
			return "encrypted_compressed_json";

		case encrypted_mp3:
			return "encrypted_mp3";

		case unknown:
		default:
			return "unknown";
		}
	}

	static const char* to_extension(const file_format_t& ff)
	{
		switch(ff) {
		case clear_binary:
			return "blob";
		case clear_xml:
			return "xml";
		case clear_wav:
			return "wav";
		case clear_mp3:
			return "mp3";
		case clear_pcap:
			return "pcap";
		case clear_email:
			return "eml";
		case clear_json:
			return "json";

		case compressed_binary:
			return "blob.bz2";
		case compressed_xml:
			return "xml.bz2";
		case compressed_wav:
			return "wav.bz2";
		case compressed_pcap:
			return "pcap.bz2";
		case compressed_email:
			return "eml.bz2";
		case compressed_json:
			return "json.bz2";

		case encrypted_compressed_binary:
			return "blob.bz2.bf";
		case encrypted_compressed_xml:
			return "xml.bz2.bf";
		case encrypted_compressed_wav:
			return "wav.bz2.bf";
		case encrypted_compressed_pcap:
			return "pcap.bz2.bf";
		case encrypted_compressed_email:
			return "eml.bz2.bf";
		case encrypted_compressed_json:
			return "json.bz2.bf";

		case encrypted_mp3:
			return "mp3.bf";

		case unknown:
		default:
			return "unk";
		}
	}

	struct convert_action
	{
		enum convert_action_t
		{
			nothing   = 0,
			compress,
			encrypt,
			decrypt,
			uncompress,
			transcode /* wav -> mp3 OR mp3 -> wav */
		};

		static inline_ const char* to_string(const convert_action_t& convert_action)
		{
			static const char* str[] = {
					"nothing",
					"compress",
					"encrypt",
					"decrypt",
					"uncompress",
					"transcode",
			};

			if(convert_action >= nothing && convert_action <= transcode)
				return str[convert_action];

			return "unknown";
		}
	};

	/**
	 * handy function to put the format components together
	 *
	 * @param compressed
	 * @param encrypted
	 * @param dsf
	 * @return
	 */
	static inline_ file_format_t make(const bool compressed, const bool encrypted, const file_format_t ff)
	{
		return make(compressed, encrypted, data_sub_format::data_sub_format_t(ff & 0xff));
	}

	/**
	 * handy function to put the format components together
	 *
	 * @param compressed
	 * @param encrypted
	 * @param dsf
	 * @return
	 */
	static inline_ file_format_t make(const bool compressed, const bool encrypted, const data_sub_format::data_sub_format_t dsf)
	{
		uint16_t df = data_format::clear;
		if(compressed && (dsf != data_sub_format::mp3)) // XXX fix this special case
			df |= data_format::compressed;
		if(encrypted)
			df |= data_format::encrypted;

		return file_format_t((df << 8 ) | dsf);
	}

	static inline_ bool is_encrypted(const file_format_t& ff)
	{
		return ff & ( data_format_mask::encrypted << 8 );
	}

	static inline_ bool is_compressed(const file_format_t& ff)
	{
		return ff & ( data_format_mask::compressed << 8 );
	}

	/**
	 * Implements the state table for the file format transformation
	 *
	 * @param from
	 * @param to
	 * @return
	 */
	static inline_ std::pair<convert_action::convert_action_t, file_format_t> get_next(const file_format_t& from, const file_format_t& to)
	{
/*
		LogDebug("from  : " << RML_HEX(4, from));
		LogDebug("to    : " << RML_HEX(4, to));
#ifndef RML_NO_DEBUG
#define verbose_return( ret_action, ret_format ) \
	do { \
		LogDebug("result: [" << RML_HEX(4, ret_action) << ", " << RML_HEX(4, (ret_format) ) << ']'); \
		return std::make_pair(ret_action, file_format_t( ret_format )); \
	} while(false)
#else
*/
#define verbose_return( ret_action, ret_format ) \
		return std::make_pair(ret_action, file_format_t( ret_format ));
//#endif

		if(from == to)
			verbose_return( convert_action::nothing, from );

		data_sub_format::data_sub_format_t dsf_from = (data_sub_format::data_sub_format_t)(from & 0xff);
		bool from_encrypted = is_encrypted(from);
		bool from_compressed = is_compressed(from);

		data_sub_format::data_sub_format_t dsf_to = (data_sub_format::data_sub_format_t)(to & 0xff);
		bool to_encrypted = is_encrypted(to);
		bool to_compressed = is_compressed(to);

		if( (data_sub_format::unknown == dsf_from) || ( data_sub_format::unknown == dsf_to) )
			verbose_return(convert_action::nothing, file_format::unknown);

		if(from_encrypted)
			verbose_return(convert_action::decrypt, from & ~(data_format_mask::encrypted << 8));

		if(from_compressed)
		{
			if(to_encrypted && to_compressed && (dsf_from == dsf_to))
				verbose_return(convert_action::encrypt, from | (data_format::encrypted << 8));

			verbose_return(convert_action::uncompress, from & ~(data_format_mask::compressed << 8));
		}

		if(dsf_from != dsf_to)
			verbose_return(convert_action::transcode, dsf_to);

		if(to_compressed) {
			// we do not want to compress already compressed audio
			// we should never get here and must be handled by the calling code
			if(dsf_from == data_sub_format::mp3)
				verbose_return(convert_action::nothing, file_format::unknown);

			verbose_return(convert_action::compress, from | (data_format::compressed << 8));
		}

		if(to_encrypted)
			verbose_return(convert_action::encrypt, from | (data_format::encrypted << 8));

		verbose_return(convert_action::nothing, file_format::unknown);

#undef verbose_return

	}
};


struct caseless_string_key_compare
{
	bool operator()(const std::string& lhs, const std::string& rhs) const
	{
		return 0 > strcasecmp(lhs.c_str(), rhs.c_str());
	}
};

typedef std::map < std::string, std::string, caseless_string_key_compare > caseless_key_string_map;

class metadata : public std::multimap<std::string, std::string>
{
	bool validate(const std::string& key)
	{
		// sanity check the current state before we add a new key and its value
		// a way to make sure we do not allow certain keys to occur more than one time.

		static const char* single_occurance_keys[] = {
				"MD5",
				"CreateTime",
				"RetentionPolicyID",
				"LegUUID",
				"OriginatorUUID",
				"OrgID",
				"UserIDs",
				"FileType",
				"ContentLength",
				"CallOwnerOrg",
				"CallOwnerUserIDs",

				"\0"
		};

		bool validate_single_occurance = false;
		for(unsigned int i = 0; '\0' != single_occurance_keys[i][0]; ++i) {
			if(key == single_occurance_keys[i]) {
				validate_single_occurance = true;
				break;
			}
		}

		return validate_single_occurance? (end() == find(key)) : true;
	}

public:
	typedef boost::shared_ptr<metadata> pointer;

	metadata(const std::string& str)
	{
		std::istringstream istr(str);
		read_data(istr);
	}

	metadata(std::istream& istr)
	{
		read_data(istr);
	}

	template< typename T >
	inline_ void add(const std::string& key, const T& value)
	{
		std::ostringstream ostr;
		ostr << value;

		if(!validate(key))
			THROW_RML_EXCEPTION("metadata key '"<<key<<"' can occur only once");

		insert(std::make_pair(key, ostr.str()));
	}

	inline_ void add(const std::string& key, const std::string& value)
	{
		if(!validate(key))
			THROW_RML_EXCEPTION("metadata key '"<<key<<"' can occur only once");

		insert(std::make_pair(key, value));
	}

	void make_headers(caseless_key_string_map& headers) const
	{
		const_iterator first_itr = begin();
		const_iterator end_itr = end();

		while(first_itr == end_itr) {
			const_iterator last = std::adjacent_find(first_itr, end_itr);

			std::string map_value;
			std::string map_key = (*first_itr).first;

			do {
				if(map_value.empty())
					map_value = (*first_itr).second;
				else {
					map_value += ',';
					map_value += (*first_itr).second;
				}
				++first_itr;
			} while(first_itr != last);

			headers.insert(std::make_pair(map_key, map_value));
		}
	}

	std::string to_string() const
	{
		std::ostringstream ostr;

		const_iterator itr = begin();
		const_iterator end_itr = end();

		while(itr != end_itr) {
			ostr << (*itr).first << ": " << (*itr).second<< "\r\n";
			++itr;
		}
		ostr << "\r\n";

		return ostr.str();
	}

	inline_ operator std::string () const
	{
		return to_string();
	}

private:
	void read_data(std::istream& istr)
	{
		std::string line;
		while(std::getline(istr, line)) {
			if(line[0] == '\r')
				break;

			{
				std::string::iterator cr_itr = line.end(); --cr_itr;
				assert(*cr_itr == '\r');
				line.erase(cr_itr);
			}

			std::size_t colon_pos;
			if((colon_pos = line.find(": ")) == std::string::npos) {
				LogWarning("Invalid header in PUT - \""<<line<<'"');
				continue;
			}

			add(std::string(line, 0, colon_pos), std::string(line, colon_pos+2));
		}

		if(empty())
			throw rml::exception("No headers found in PUT");
	}
};

#pragma pack(push,1)
struct header_encrypted
{
	uint8_t magic[6];

	uint8_t archived_version_major;
	uint8_t archived_version_minor;
	uint8_t archived_version_patch;

	uint8_t format_version_major;
	uint8_t format_version_minor;

	uint8_t iv[8];

	/* padding to 8 bytes & for future expansion */
	uint8_t padding[21];

	header_encrypted() {
		memcpy(magic, "RMA-CE", 6);
		archived_version_major = (uint8_t)version_number[0];
		archived_version_minor = (uint8_t)version_number[1];
		archived_version_patch = (uint8_t)version_number[2];

		format_version_major = (uint8_t)file_format_version::major;
		format_version_minor = (uint8_t)file_format_version::minor;
		memcpy(padding, "RedMatterRedMatterRedMatterRedMatterRedMatterRedMatterRedMatter", sizeof(padding));
	}
};
#pragma pack(pop)

// filesize union for headers
//#pragma pack(push,1)
//union file_size_t {
//	uint8_t as_bytes[sizeof(uint64_t)];
//	uint64_t as_int64;
//
//	file_size_t() : as_int64(0) {}
//	file_size_t(const uint64_t& i) : as_int64(i) {}
//	file_size_t(const size_t& s) : as_int64(s) {}
//};
//#pragma pack(pop)

#pragma pack(push,1)
struct header_precrypt
{
	uint8_t magic[3];
	uint8_t format;
	uint8_t sub_format;
	uint64_t content_length;
	uint64_t inflated_size;
	uint32_t crc32;

	uint8_t padding[7];

	header_precrypt() {
		memcpy(magic, "RMA", 3);
		memcpy(padding, "RedMatterRedMatterRedMatterRedMatterRedMatterRedMatterRedMatter", sizeof(padding));
	}
};
#pragma pack(pop)

struct purge_def
{
	struct org_def
	{
		archiving::org_id_t org_id;
		uint64_t max_retention_id;

		org_def(const archiving::org_id_t& org_id, const uint64_t& max_retention_id = 0) : org_id(org_id), max_retention_id(max_retention_id)
		{
		}

		struct compare
		{
			bool operator()(const org_def& lhs, const org_def& rhs) const
			{
				if(lhs.org_id != rhs.org_id)
					return lhs.org_id < rhs.org_id;

				return lhs.max_retention_id < rhs.max_retention_id;
			}
		};
	};

	typedef std::set< org_def, org_def::compare > set_t;

	set_t set;
	unsigned int offset;
	unsigned int limit;
	time_t creation_time;

	typedef boost::shared_ptr< purge_def > pointer;

	explicit purge_def(const time_t creation_time = 0) : offset(0), limit(0), creation_time(creation_time)
	{
	}
};

}  // namespace types

}  // namespace app

#endif /* APP_TYPES_HPP_ */
