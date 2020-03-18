#undef RML_LOG_SOURCE_LOCATION
#define NO_BOOST_LOGGING

#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstddef>

#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/ref.hpp>
#include <boost/noncopyable.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>

#include <cryptopp/crc.h>
#include <cryptopp/files.h>
#include <cryptopp/filters.h>

#include <rml/config.h>
#include <rml/logger.h>
#include <rml/utils.h>

#include "../types.hpp"
#include "../compress.hpp"
#include "../crypto.hpp"
#include "../coreapi.hpp"

namespace app {
std::string operation_data::temp_files_base_directory;
}

int main(int argc, char* argv[]) try
{
	boost::program_options::variables_map cli_confMap;
	{
		boost::program_options::options_description cli_optsDesc(
					boost::str(boost::format(
							"Archiving Extract Utility v%1%"
							"\nUtility to convert a 'stored' file to its original format"
							"\n"
							"\n%2%\n\nOptions"
							) % version_number_string % copyright_string));
		cli_optsDesc.add_options()
				("source", boost::program_options::value<std::string>(), "read from file")
				("target", boost::program_options::value<std::string>(), "write to file")
				("api-url", boost::program_options::value<std::string>()->default_value("http://cdr.coreapi.redmatter.com"), "CoreAPI URL")
				("api-auth-hash", boost::program_options::value<std::string>(), "CoreAPI auth hash")
				("org-id", boost::program_options::value<unsigned int>(), "CoreAPI auth hash")
				("help", "this help")
				;
		boost::program_options::store(boost::program_options::parse_command_line(argc, argv, cli_optsDesc), cli_confMap);
		boost::program_options::notify(cli_confMap);

		if (cli_confMap.count("help")) {
			std::cout << cli_optsDesc << "\n";
			return 1;
		}
	}

	app::operation_data::temp_files_base_directory = ".";

	if(1 != cli_confMap.count("org-id"))
		throw rml::exception("org-id not given");

	if(1 != cli_confMap.count("source"))
		throw rml::exception("source not given");

	if(1 != cli_confMap.count("target"))
		throw rml::exception("target not given");

	const unsigned int& org_id(cli_confMap["org-id"].as<unsigned int>());

	app::types::header_precrypt ph;

	rml::utils::tmp_fstream::pointer p_source(new rml::utils::tmp_fstream(cli_confMap["source"].as<std::string>(), std::fstream::in | std::fstream::binary));
	p_source->unsetf(std::ios_base::skipws);

	unsigned int iter = 0;
	{
		const std::size_t& max_header_size = std::max<std::size_t>(sizeof(app::types::header_encrypted), sizeof(app::types::header_precrypt));
		std::vector<char> header(max_header_size);
		app::types::header_encrypted eh;

		p_source->read(&header[0], max_header_size);
		p_source->seekg(0);

		if(0 == memcmp(&header[0], eh.magic, sizeof(eh.magic))) {
			LogMessage("RMA-CE file format");
			if(1 != cli_confMap.count("api-auth-hash"))
				throw rml::exception("api-auth-hash not given");
			iter = 3;
		}
		else if(0 == memcmp(&header[0], ph.magic, sizeof(ph.magic))) {
			LogMessage("RMA file format");
			iter = 2;
		}
		else {
			LogMessage("Unknown file format");
			return 1;
		}
	}

	rml::utils::tmp_fstream::pointer p_target(new rml::utils::tmp_fstream(cli_confMap["target"].as<std::string>(), std::fstream::in | std::fstream::out | std::fstream::binary | std::fstream::trunc));
	rml::utils::tmp_fstream::pointer p_tmp(new rml::utils::tmp_fstream(cli_confMap["target"].as<std::string>(), std::size_t(0), "."));

	while(iter) {
		p_source->seekg(0);
		switch(iter) {
		default:
		case 0:
			break;

		case 1:
		{
			std::size_t size = app::bzip2::decompress(*p_source, *p_target);
			LogDebug("Inflated to "<<size<<" bytes");
			if(ph.crc32) {
				uint32_t crc32;
				p_target->seekg(0);
				CryptoPP::CRC32 hash;
				CryptoPP::FileSource(*p_target, true,
						new CryptoPP::HashFilter(hash,
								new CryptoPP::ArraySink((byte *)&crc32, CryptoPP::CRC32::DIGESTSIZE)));

				LogMessage("crc32: " << RML_HEX(8, crc32) << RML_HEX_RESET << " file: "<<p_target->get_filename());

				if(crc32 != ph.crc32)
					throw rml::exception("CRC mismatch");
			}

			p_target->set_persistent(true);
			return 0;
		}

		case 2:
		{
			p_source->read((char *)&ph, sizeof(app::types::header_precrypt));
			LogMessage("header::magic: " << ph.magic[0]<<ph.magic[1]<<ph.magic[2]);
			LogMessage("header::format: " << RML_HEX(2, (int)ph.format));
			LogMessage("header::sub_format: " << RML_HEX(2, (int)ph.sub_format) << RML_HEX_RESET);
			LogMessage("header::content_length: " << ph.content_length);
			LogMessage("header::inflated_size: " << ph.inflated_size);
			LogMessage("header::crc32: " << RML_HEX(8, ph.crc32) << RML_HEX_RESET);

			if(ph.content_length == ph.inflated_size)
				p_tmp = p_target;
			else
				LogVerbose("Different content-length; assuming compressed");

			(*p_tmp) << p_source->rdbuf();

			if(ph.content_length == ph.inflated_size) {
				p_tmp->set_persistent(true);
				iter -= 2;
			}
			else {
				p_tmp->set_persistent(false);
				p_source = p_tmp;
				iter--;
			}
			break;
		}

		case 3:
		{
			app::coreapi coreapi(cli_confMap["api-url"].as<std::string>(), cli_confMap["api-auth-hash"].as<std::string>());
			app::utils::crypto crypto;
			app::storage::s3::endpoint::pointer p_ep = app::storage::s3::endpoint::finder(coreapi, org_id);
			crypto.decrypt(*p_source, *p_tmp, p_ep->encryption_key);
			p_source = p_tmp;
			p_tmp.reset(new rml::utils::tmp_fstream(cli_confMap["target"].as<std::string>(), std::size_t(0), "."));
			iter--;
			break;
		}

		}
	}
}
catch(const std::exception& e) {
	LogError("Exception: "<<e.what());
	return 1;
}
catch(...) {
	LogError("Unexpected exception");
	return 1;
}
