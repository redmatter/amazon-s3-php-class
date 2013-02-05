#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1

#include "archived.hpp"

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

#ifndef DEFAULT_APP_LONG_NAME
#define DEFAULT_APP_LONG_NAME "Archiving Convert"
#endif

int main(int argc, char* argv[])
{
	bool fs_mode(false);

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
					("source-key", boost::program_options::value<std::string>(), "Download the object identified by 'key' and make it the source")
					("archived-host", boost::program_options::value<std::string>()->default_value("archived.redmatter.com"), "ArchiveD hostname OR URL from which the 'source-key' is to be downloaded")
					("access-reason", boost::program_options::value<std::string>(), "Reason for access to the onbject from ArchiveD")
					("target", boost::program_options::value<std::string>(), "Write to file")
					("verify", "Verify the source file by converting it to plain format and comparing checksum")
					("fs", "Uses FreeSWITCH's system() invocation semantics along with return code")
					("api-host", boost::program_options::value<std::string>()->default_value("api.redmatter.com"), "Core API hostname OR URL")
					("api-token", boost::program_options::value<std::string>(), "Core API token")
					("key", boost::program_options::value<std::string>(), "Encryption key")
					("temp-dir", boost::program_options::value<std::string>()->default_value("/tmp"), "Temporary files will be put here")
					("help", "this help")
					("version", "shows version information")
					("log.syslog-tag", boost::program_options::value<std::string>(), "syslog tag to be used ( relevant for rsyslog filter )")
					;
			rml::logger::describe_config(cli_optsDesc);

			boost::program_options::store(boost::program_options::parse_command_line(argc, argv, cli_optsDesc), cli_confMap);
			boost::program_options::notify(cli_confMap);

#define fs_exit(code) \
	do { \
		if(fs_mode) \
			std::cout << ((!code)? "OK": "ERR") << std::endl; \
		exit(code); \
	} while(false)

			if (cli_confMap.count("help")) {
				std::cout << cli_optsDesc << "\n";
				fs_exit(rml::utils::exit_codes::failure);
			}

			fs_mode = (cli_confMap.count("fs") != 0);

			if(cli_confMap.count("version")) {
				std::cout << boost::str(boost::format(DEFAULT_APP_LONG_NAME" v%1%\n%2%\n") % version_number_string % copyright_string);
				fs_exit(rml::utils::exit_codes::normal);
			}

			rml::logger::configure(cli_confMap,
					cli_confMap.count("log.syslog-tag")?
							cli_confMap["log.syslog-tag"].as<std::string>().c_str(): basename(argv[0]));
		}

		const bool source_key_given(cli_confMap.count("source-key"));
		if(!(cli_confMap.count("source") || source_key_given))
			throw rml::exception("Source not given");

		if(source_key_given && !cli_confMap.count("api-token"))
			throw rml::exception("API token not given");

		if( (1 != cli_confMap.count("target")) && (0 == cli_confMap.count("verify")) )
			throw rml::exception("Target path not given");

		const std::string& temp_dir(cli_confMap["temp-dir"].as<std::string>());

		app::types::header_precrypt ph;

		bool source_plain(false);
		rml::utils::tmp_fstream::pointer p_source;
		{
			const std::string& source_path(cli_confMap[source_key_given? "source-key": "source"].as<std::string>());

			if(source_key_given) {
				// create a temp file readied for the file from ArchiveD
				rml::utils::tmp_fstream::pointer p_tmp_source = rml::utils::tmp_fstream::create(
						boost::replace_all_copy(source_path, "/", "_"), std::size_t(0), temp_dir);

				boost::asio::io_service io_service;
				boost::asio::io_service::work work(io_service); // make sure we keep it proped up!

				app::archived archived(
						io_service,
						cli_confMap["archived-host"].as<std::string>(),
						cli_confMap["api-host"].as<std::string>(),
						boost::str(boost::format("%1%:%2%.%3%") % DEFAULT_APP_NAME % version_number[2] % version_number[3]));

				const app::types::file_format::file_format_t& file_format(
						archived.get(source_path, cli_confMap["api-token"].as<std::string>(), p_tmp_source,
								cli_confMap.count("access-reason")? cli_confMap["access-reason"].as<std::string>(): ""));

				p_source = p_tmp_source;

				source_plain = (!app::types::file_format::is_compressed(file_format) &&
						!app::types::file_format::is_encrypted(file_format));
				p_source->set_persistent(source_plain);
			}
			else {
				p_source = rml::utils::tmp_fstream::create(source_path, std::fstream::in | std::fstream::binary);

				const std::string tmp_source_path(temp_dir + "/" + p_source->get_filename(true));
				if (!p_source->quick_copy(tmp_source_path))
					throw rml::exception("Failed to create temporary copy of source file");

				p_source.reset();

				p_source = rml::utils::tmp_fstream::create(tmp_source_path, std::fstream::in | std::fstream::binary);
				p_source->set_persistent(false);
			}
		}

		unsigned int iter = 0;
		if(!source_plain) {
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
				fs_exit(rml::utils::exit_codes::failure);
			}
		}

		// if we need to iterate and convert the file
		if(iter) {
			rml::utils::tmp_fstream::pointer p_target;
			if(cli_confMap.count("target"))
				p_target = rml::utils::tmp_fstream::create(cli_confMap["target"].as<std::string>(), std::fstream::in | std::fstream::out | std::fstream::binary | std::fstream::trunc);
			else if(cli_confMap.count("verify"))
				p_target = rml::utils::tmp_fstream::create(p_source->get_filename(), std::size_t(0), temp_dir);

			rml::utils::tmp_fstream::pointer p_tmp = rml::utils::tmp_fstream::create(p_source->get_filename(), std::size_t(0), temp_dir);

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

					app::crypto crypto;
					crypto.decrypt(*p_source, *p_tmp, enc_key);
					p_source = p_tmp;
					p_tmp = rml::utils::tmp_fstream::create(p_source->get_filename(), std::size_t(0), temp_dir);
					iter--;
					break;
				}

				}
			}
		}
	}
	catch(const std::exception& e) {
		LogError("Exception: "<<e.what());
		fs_exit(rml::utils::exit_codes::failure);
	}
	catch(...) {
		LogError("Unexpected exception");
		fs_exit(rml::utils::exit_codes::failure);
	}

	fs_exit(rml::utils::exit_codes::normal);
}
