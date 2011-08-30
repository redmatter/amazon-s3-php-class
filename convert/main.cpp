#include "../archived/types.hpp"
#include "../archived/compress.hpp"
#include "../archived/crypto.hpp"
#include "../archived/storage_endpoint.hpp"
#include "../common/coreapi.hpp"

#include <rml/logger.h>
#include <rml/config.h>
#include <rml/utils.h>

#include <cryptopp/crc.h>
#include <cryptopp/files.h>
#include <cryptopp/filters.h>

#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/ref.hpp>
#include <boost/noncopyable.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>

#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstddef>

#include <libgen.h>

/*
namespace app {
std::string operation_data::temp_files_base_directory;
}
*/

int main(int argc, char* argv[])
{
	try
	{
		rml::logger::configure();

		boost::program_options::variables_map cli_confMap;
		{
			boost::program_options::options_description cli_optsDesc(
						boost::str(boost::format(
								"%1% v%2%"
								"\nUtility to convert a 'stored' file to its original format"
								"\n"
								"\n%3%\n\nOptions"
								) % DEFAULT_APP_LONG_NAME % version_number_string % copyright_string));
			cli_optsDesc.add_options()
					("source", boost::program_options::value<std::string>(), "Read from file")
					("target", boost::program_options::value<std::string>(), "Write to file")
					("verify", "Verify the source file by converting it to plain format and comparing checksum")
					("key", boost::program_options::value<std::string>(), "Encryption key")
					("log.file_path", boost::program_options::value<std::string>()->default_value("console"), "Logging goes to a file or syslog or console")
					("log.level", boost::program_options::value<int>()->default_value(4), "Logging level")
					("log.syslog-tag", boost::program_options::value<std::string>(), "syslog tag to be used ( relevant for rsyslog filter )")
					("temp-dir", boost::program_options::value<std::string>()->default_value("/tmp"), "Temporary files will be put here")
					("help", "this help")
					;
			boost::program_options::store(boost::program_options::parse_command_line(argc, argv, cli_optsDesc), cli_confMap);
			boost::program_options::notify(cli_confMap);

			if (cli_confMap.count("help")) {
				std::cout << cli_optsDesc << "\n";
				exit(rml::utils::exit_codes::failure);
			}

			if (cli_confMap.count("log.syslog-tag"))
				rml::logger::set_app_name(cli_confMap["log.syslog-tag"].as<std::string>());
			else
				rml::logger::set_app_name(basename(argv[0]));

			rml::logger::configure(cli_confMap);
		}

		if(1 != cli_confMap.count("source"))
			throw rml::exception("Source path not given");

		if( (1 != cli_confMap.count("target")) && (0 == cli_confMap.count("verify")) )
			throw rml::exception("Target path not given");

	//	app::operation_data::temp_files_base_directory = cli_confMap["temp-dir"].as<std::string>();

		app::types::header_precrypt ph;

		rml::utils::tmp_fstream::pointer p_source(new rml::utils::tmp_fstream(cli_confMap["source"].as<std::string>(), std::fstream::in | std::fstream::binary));

		unsigned int iter = 0;
		{
			const std::size_t& max_header_size = std::max<std::size_t>(sizeof(app::types::header_encrypted), sizeof(app::types::header_precrypt));
			std::vector<char> header(max_header_size);
			app::types::header_encrypted eh;

			p_source->read(&header[0], max_header_size);
			p_source->seekg(0);

			if(0 == memcmp(&header[0], eh.magic, sizeof(eh.magic))) {
				LogVerbose("RMA-CE file format");
				if(1 != cli_confMap.count("key"))
					throw rml::exception("Encryption key not given");

				iter = 3;
			}
			else if(0 == memcmp(&header[0], ph.magic, sizeof(ph.magic))) {
				LogVerbose("RMA file format");
				iter = 2;
			}
			else {
				LogError("Unknown file format");
				exit(rml::utils::exit_codes::failure);
			}
		}

		rml::utils::tmp_fstream::pointer p_target;
		if(cli_confMap.count("target"))
			p_target.reset(new rml::utils::tmp_fstream(cli_confMap["target"].as<std::string>(), std::fstream::in | std::fstream::out | std::fstream::binary | std::fstream::trunc));
		else if(cli_confMap.count("verify"))
			p_target.reset(new rml::utils::tmp_fstream(cli_confMap["source"].as<std::string>(), std::size_t(0), cli_confMap["temp-dir"].as<std::string>()));

		rml::utils::tmp_fstream::pointer p_tmp(new rml::utils::tmp_fstream(cli_confMap["source"].as<std::string>(), std::size_t(0), cli_confMap["temp-dir"].as<std::string>()));

		while(iter) {
			p_source->seekg(0);
			switch(iter) {
			default:
			case 0:
				break;

			case 1:
			{
				std::size_t size = app::bzip2::decompress(*p_source, *p_target);
				LogVerbose("Inflated to "<<size<<" bytes");
				if(ph.crc32) {
					uint32_t crc32;
					p_target->seekg(0);
					CryptoPP::CRC32 hash;
					CryptoPP::FileSource(*p_target, true,
							new CryptoPP::HashFilter(hash,
									new CryptoPP::ArraySink((byte *)&crc32, CryptoPP::CRC32::DIGESTSIZE)));

					LogVerbose("crc32: " << RML_HEX(8, crc32) << RML_HEX_RESET << " file: "<<p_target->get_filename());

					if(crc32 != ph.crc32)
						throw rml::exception("CRC mismatch");
				}

				if(cli_confMap.count("target"))
					LogMessage("Successfully converted");
				else
					LogMessage("File verified; CRC32 checked OK");

				iter--;
				break;
			}

			case 2:
			{
				p_source->read((char *)&ph, sizeof(app::types::header_precrypt));
				LogVerbose("header::magic: " << ph.magic[0]<<ph.magic[1]<<ph.magic[2]);
				LogVerbose("header::format: " << RML_HEX(2, (int)ph.format));
				LogVerbose("header::sub_format: " << RML_HEX(2, (int)ph.sub_format) << RML_HEX_RESET);
				LogVerbose("header::content_length: " << ph.content_length);
				LogVerbose("header::inflated_size: " << ph.inflated_size);
				LogVerbose("header::crc32: " << RML_HEX(8, ph.crc32) << RML_HEX_RESET);

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
				app::storage::basic_endpoint::encryption_key_t enc_key;
				const std::string& key(cli_confMap["key"].as<std::string>());
				CryptoPP::StringSource((const byte*)&key[0], key.length(), true,
						new CryptoPP::Base64Decoder(
								new CryptoPP::ArraySink(&enc_key[0], app::storage::basic_endpoint::encryption_key_t::static_size)));

				app::utils::crypto crypto;
				crypto.decrypt(*p_source, *p_tmp, enc_key);
				p_source = p_tmp;
				p_tmp.reset(new rml::utils::tmp_fstream(cli_confMap["source"].as<std::string>(), std::size_t(0), cli_confMap["temp-dir"].as<std::string>()));
				iter--;
				break;
			}

			}
		}
	}
	catch(const std::exception& e) {
		LogError("Exception: "<<e.what());
		exit(rml::utils::exit_codes::failure);
	}
	catch(...) {
		LogError("Unexpected exception");
		exit(rml::utils::exit_codes::failure);
	}

	exit(rml::utils::exit_codes::normal);
}
