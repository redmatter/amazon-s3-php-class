/*
 * types.hpp
 *
 *  Created on: 02 Feb 2011
 *      Author: dkorah
 */

#ifndef APP_TYPES_HPP_
#define APP_TYPES_HPP_

#include <stdint.h>
#include <string.h>

#include "../common/defines.h"

#include "globals.h"

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
| | [-CRC24-]                     3 bytes | |
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
* 0x00 application/octet-stream
* 0x01 application/xml  OR xml
* 0x02 audio/x-wav      OR wav
* 0x03 audio/mpeg       OR mp3
*/

namespace app {

namespace types {

struct file_format_version {
	enum {
		major = 1,
		minor = 0
	};
};

struct data_format {
	enum data_format_t {
		clear      = 0x00,
		compressed = 0x01,
		encrypted  = 0x10
	};
};

struct data_format_mask {
	enum data_format_mask_t {
		compressed = 0x0f,
		encrypted  = 0xf0
	};
};

struct data_sub_format {
	enum data_sub_format_t {
		binary = 0x00, // blob (default / ignore content)
		xml    = 0x01,
		wav    = 0x02,
		mp3    = 0x03
	};
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

struct file_format {
	enum file_format_t {
		clear_binary                 = ( data_format::clear << 8 ) | data_sub_format::binary,
		clear_xml                    = ( data_format::clear << 8 ) | data_sub_format::xml,
		clear_wav                    = ( data_format::clear << 8 ) | data_sub_format::wav,
		clear_mp3                    = ( data_format::clear << 8 ) | data_sub_format::mp3,

		compressed_binary            = ( data_format::compressed << 8 ) | data_sub_format::binary,
		compressed_xml               = ( data_format::compressed << 8 ) | data_sub_format::xml,
		compressed_wav               = ( data_format::compressed << 8 ) | data_sub_format::wav,

		encrypted_compressed_binary  = (( data_format::encrypted | data_format::compressed ) << 8 ) | data_sub_format::binary,
		encrypted_compressed_xml     = (( data_format::encrypted | data_format::compressed ) << 8 ) | data_sub_format::xml,
		encrypted_compressed_wav     = (( data_format::encrypted | data_format::compressed ) << 8 ) | data_sub_format::wav,

		encrypted_mp3                = ( data_format::encrypted << 8 ) | data_sub_format::mp3
	};

	/**
	 * Implements the state table for the file format transformation
	 *
	 * @param from
	 * @param to
	 * @return
	 */
	static inline file_format_t get_next_state(const file_format_t& from, const file_format_t& to) {
		if(from == to)
			return from;

		LogDebug("from  : " << RML_HEX(4, from));
		LogDebug("to    : " << RML_HEX(4, to));

		data_sub_format::data_sub_format_t dsf_from = (data_sub_format::data_sub_format_t)(from & 0xff);
		bool from_encrypted = (bool)(from & ( data_format_mask::encrypted << 8 ));
		bool from_compressed = (bool)(from & ( data_format_mask::compressed << 8 ));

		data_sub_format::data_sub_format_t dsf_to = (data_sub_format::data_sub_format_t)(to & 0xff);
		bool to_encrypted = (bool)(to & ( data_format_mask::encrypted << 8 ));
		bool to_compressed = (bool)(to & ( data_format_mask::compressed << 8 ));

#if DEBUG >= 1
#define verbose_return( ret ) \
	do { \
		file_format_t  ret_ = file_format_t( ret ); \
		LogDebug("result: " << RML_HEX(4, ret_)); \
		return ret_; \
	} while(false)
#else
#define verbose_return( ret ) \
	return file_format_t( ret );
#endif

		if(from_encrypted)
			verbose_return(from & ~(data_format_mask::encrypted << 8));

		if(from_compressed)
		{
			if(to_encrypted && to_compressed && (dsf_from == dsf_to))
				verbose_return(from | (data_format::encrypted << 8));

			verbose_return(from & ~(data_format_mask::compressed << 8));
		}

		if(dsf_from != dsf_to)
			verbose_return(dsf_to);

		if(to_compressed)
			verbose_return(from | (data_format::compressed << 8));

		if(to_encrypted)
			verbose_return(from | (data_format::encrypted << 8));

#undef verbose_return

	}
};


class put_metadata :
	public std::multimap<std::string, std::string>
{
	bool validate(const std::string& key) {
		// sanity check the current state before we add a new key and its value
		// a way to make sure we do not allow certain keys to occur more than one time.

		static const char* single_occurance_keys[] = {
				"md5",
				"CreateTime",
				"RetentionPolicyID",
				"LegUUID",
				"ResourceID",
				"OrgID",
				"UserID",
				"FileType",
				"ContentLength"
		};

		bool validate_single_occurance = false;
		for(int i = 0; i < sizeof(single_occurance_keys); ++i) {
			if(key == single_occurance_keys[i]) {
				validate_single_occurance = true;
				break;
			}
		}

		return validate_single_occurance? (end() == find(key)) : true;
	}

	void read_data(std::istream& istr);

public:
	typedef boost::shared_ptr<put_metadata> pointer;

	put_metadata(const std::string& str) {
		std::istringstream istr(str);
		read_data(istr);
	}

	put_metadata(std::istream& istr) {
		read_data(istr);
	}

	template< typename T >
	inline void add(const std::string& key, const T& value) {
		std::ostringstream ostr;
		ostr << value;

		if(!validate(key))
			THROW_RML_EXCEPTION("metadat key '"<<key<<"' can occur only once");

		insert(std::make_pair(key, ostr.str()));
	}

	template<>
	inline void add(const std::string& key, const std::string& value) {
		if(!validate(key))
			THROW_RML_EXCEPTION("metadat key '"<<key<<"' can occur only once");

		insert(std::make_pair(key, value));
	}

private:
	void read_data(std::istream& istr) {
		std::string line;
		int line_number = 0;
		while(!std::getline(istr, line)) {
			if(line[0] == '\r')
				break;

			{
				std::string::iterator cr_itr = line.end(); cr_itr--;
				assert(*cr_itr == '\r');
				line.erase(cr_itr);
			}

			LogDebug("line: "<<++line_number<<": "<<line);

			std::size_t colon_pos;
			if((colon_pos = line.find(": ")) == std::string::npos) {
				LogWarning("Invalid header in PUT - \""<<line<<'"');
				continue;
			}

			add(std::string(line, 0, colon_pos - 1), std::string(line, colon_pos+2));
		}

		if(empty())
			throw rml::exception("No headers found in PUT");
	}
};

#pragma pack(push,1)
struct header_encrypted {
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
struct header_precrypt {
	uint8_t magic[3];
	uint8_t format;
	uint8_t sub_format;
	uint64_t content_length;
	uint64_t inflated_size;
	uint8_t crc24[3];

	header_precrypt() {
		memcpy(magic, "RMA", 3);
	}
};
#pragma pack(pop)

}  // namespace types

}  // namespace app

#endif /* APP_TYPES_HPP_ */
