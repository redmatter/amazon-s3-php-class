#ifndef MIGRATION_DATA_HPP
#define MIGRATION_DATA_HPP

#include "../archived/storage_endpoint.hpp"
#include "../archived/types.hpp"

#include "../common/types.hpp"
#include "../common/archivable.hpp"

#include <rml/utils.h>

#include <string>
#include <sstream>
#include <utility>

namespace app {
struct migration_data;
}

namespace std {
ostream& operator<<(ostream& ostr, const app::migration_data& md);
}

namespace app {

extern std::string temp_files_base_directory;

struct migration_data
{
	enum state_t
	{
		init = 0,
		converted,
		copied
	};

	uint64_t id;
	std::string uuid;
	std::string path;
	rml::utils::tmp_fstream::pointer p_fstream;
	archiving::org_id org;
	app::types::file_format::file_format_t target_format;
	std::string identifier;
	time_t create_time;

	storage::s3::endpoint::pointer p_endpoint;

	state_t state;

	migration_data(
			uint64_t id, std::string uuid, const std::string& path, const archiving::org_id& org, const time_t& create_time,
			storage::s3::endpoint::pointer p_endpoint,
			const app::types::file_format::file_format_t& target_format = app::types::file_format::encrypted_compressed_wav,
			const archiving::archivable::file_type::file_type_t& filetype = archiving::archivable::file_type::call_recording) :

			id(id), uuid(uuid), path(path), org(org), target_format(target_format), create_time(create_time), p_endpoint(p_endpoint)
	{
		if(!p_endpoint)
			throw rml::exception("Storage endpoint not found");

		std::ostringstream ostr;
		ostr << archiving::archivable::file_type::to_string(filetype) << '/' << org << uuid;
		identifier = ostr.str();
	}

	struct compare
	{
		bool operator()(const migration_data& lhs, const migration_data& rhs)
		{
			if(lhs.org != rhs.org)
				return lhs.org < rhs.org;

			if(lhs.uuid != rhs.uuid)
				return lhs.uuid < rhs.uuid;

			return lhs.path < rhs.path;
		}
	};

	typedef rml::pc_channel<migration_data> channel_t;

	friend std::ostream& std::operator<<(std::ostream& ostr, const migration_data& md);
};

}

namespace std {
ostream& operator<<(ostream& ostr, const app::migration_data& md) {
	return ostr << md.identifier;
}
}

#endif // MIGRATION_DATA_HPP
