#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1

#include "../common/macros.h"

#include "../../archived/types.hpp"
#include "../../archived/compress.hpp"
#include "../../archived/crypto.hpp"
#include "../../archived/storage_endpoint.hpp"

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
#include <boost/array.hpp>

#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstddef>

#include <sys/stat.h>
#include <errno.h>

int main(int argc, char* argv[])
{
	try
	{
		rml::logger::configure();

		boost::program_options::variables_map cli_confMap;
		{
			boost::program_options::options_description cli_optsDesc(
					"convert-to-archive - compress and encrypt a file"
					"\nOptions", 100, 50);
			cli_optsDesc.add_options()
					("input", boost::program_options::value<std::string>(),
							"Input; Path to file on local filesystem")
					("output", boost::program_options::value<std::string>(),
							"Output; Path to output file on local filesystem")
					("content-type", boost::program_options::value<std::string>(),
							"Content type of the data; see description below")
					("encryption-key", boost::program_options::value<std::string>(),
							"Encryption key; base64 encoded")
                    ("v1", "Use version 1 header format and encryption (for legacy use or testing only)")
					("help", "Shows this help")
					("version", "Shows version information")
					("temp-dir", boost::program_options::value<std::string>()->default_value("/tmp"),
							"Temporary files will be put here")
					("log.syslog-tag", boost::program_options::value<std::string>(),
							"Syslog tag to be used ( relevant for rsyslog filter )")
					;
			rml::logger::describe_config(cli_optsDesc);

			boost::program_options::store(
					boost::program_options::parse_command_line(argc, argv, cli_optsDesc), cli_confMap);
			boost::program_options::notify(cli_confMap);

			if (cli_confMap.count("help")) {
				std::cout << cli_optsDesc << "\n";
				exit(rml::utils::exit_codes::failure);
			}

			if (cli_confMap.count("version")) {
				std::cout << boost::str(boost::format("v%1%\n") % version_number_string);
				exit(rml::utils::exit_codes::normal);
			}

			rml::logger::configure(cli_confMap,
					cli_confMap.count("log.syslog-tag")?
							cli_confMap["log.syslog-tag"].as<std::string>().c_str(): basename(argv[0]));
		}

		check_cli_option(cli_confMap, "input", "Input path not given");
		check_cli_option(cli_confMap, "output", "Output path not given");
		check_cli_option(cli_confMap, "encryption-key", "Encryption-key not given");
		check_cli_option(cli_confMap, "content-type", "Content-type not given");
		const app::types::data_sub_format::data_sub_format_t& data_format(
				app::types::data_sub_format::from_string(cli_confMap["content-type"].as<std::string>()));

		const app::types::file_format::file_format_t& input_format(
				app::types::file_format::make(false, false, data_format));
		/*
		// TODO check cli options to decide what the output format would be; in
		// ArchiveD it is controlled by the archive policy
		const app::types::file_format::file_format_t& output_format(
				app::types::file_format::make(true, true, data_format));
		*/

		const std::string& temp_dir(cli_confMap["temp-dir"].as<std::string>());

		app::types::header_precrypt ph;

		rml::utils::tmp_fstream::pointer p_source;
		{
			p_source = rml::utils::tmp_fstream::create(
					cli_confMap["input"].as<std::string>(), std::fstream::in | std::fstream::binary);

			// make a quick copy (man 2 link) so that all temp files, which are named based on that,
			// end up in the chosen temp directory
			const std::string tmp_source_path(temp_dir + "/" + p_source->get_filename(true));

			struct stat stat_buf;
			if (0 == stat(tmp_source_path.c_str(), &stat_buf)) {
				THROW_RML_EXCEPTION(tmp_source_path<<" already exists");
			}
			if (!p_source->quick_copy(tmp_source_path)){
				THROW_RML_EXCEPTION(tmp_source_path<<" failed to create temporary copy");
			}

			// reset and open the new path; set it to be deleted when done
			p_source.reset();
			p_source = rml::utils::tmp_fstream::create(tmp_source_path, std::fstream::in | std::fstream::binary);
			p_source->set_persistent(false);
		}

		{
			char header[6];

			p_source->read(&header[0], sizeof(header));
			if(0 == memcmp(&header[0], archiving::archived::file_format::crypto_header_magic_string(), sizeof(header))) {
				LogMessage("File is already in archive format; RMA-CE header was found");
				exit(rml::utils::exit_codes::failure);
			}

			p_source->seekg(0);
		}

		rml::utils::tmp_fstream::pointer p_output = rml::utils::tmp_fstream::create(
				cli_confMap["output"].as<std::string>(), std::fstream::in | std::fstream::out | std::fstream::binary | std::fstream::trunc);

		rml::utils::tmp_fstream::pointer p_tmp = rml::utils::tmp_fstream::create(p_source->get_filename(), std::size_t(0), temp_dir);

		app::bzip2::compress(p_source, p_tmp, input_format, p_source->get_filename(true));
		{
			app::storage::basic_endpoint::encryption_key_t enc_key;
			const std::string& key(cli_confMap["encryption-key"].as<std::string>());
			CryptoPP::StringSource((const byte*)&key[0], key.length(), true,
					new CryptoPP::Base64Decoder(
							new CryptoPP::ArraySink(&enc_key[0], app::storage::basic_endpoint::encryption_key_t::static_size)));

			app::crypto crypto;
            if (cli_confMap.count("v1")) {
                crypto.encrypt(*p_tmp, *p_output, enc_key);
            } else {
                crypto.encrypt_aes(*p_tmp, *p_output, enc_key);
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
