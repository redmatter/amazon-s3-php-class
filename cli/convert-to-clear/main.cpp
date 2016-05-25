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
					"convert-to-clear - decrypt and decompress an encrypted-compressed file (from storage)"
					"\nOptions", 100, 50);
			cli_optsDesc.add_options()
					("input", boost::program_options::value<std::string>(),
							"Input; Path to file on local filesystem")
					("output", boost::program_options::value<std::string>(),
							"Output; Path to output file on local filesystem")
					("encryption-key", boost::program_options::value<std::string>(),
							"Encryption key; base64 encoded")
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
			app::types::header_encrypted eh;

			p_source->read(&header[0], sizeof(header));
			if(0 != memcmp(&header[0], eh.magic, sizeof(header))) {
				LogError("Unknown file format; no RMA-CE header was found");
				exit(rml::utils::exit_codes::failure);
			}

			p_source->seekg(0);
		}

		// iterate and convert so that decrypt step can be skipped
		unsigned int iter = 3;
		if(iter) {
			rml::utils::tmp_fstream::pointer p_output = rml::utils::tmp_fstream::create(
					cli_confMap["output"].as<std::string>(), std::fstream::in | std::fstream::out | std::fstream::binary | std::fstream::trunc);

			rml::utils::tmp_fstream::pointer p_tmp = rml::utils::tmp_fstream::create(p_source->get_filename(), std::size_t(0), temp_dir);

			while(iter) {
				p_source->seekg(0);
				switch(iter) {

				case 1:
				{
					std::size_t size = app::bzip2::decompress(*p_source, *p_output);
					LogVerbose("Inflated to "<<size<<" bytes");
					if(ph.crc32) {
						uint32_t crc32;
						p_output->seekg(0);
						CryptoPP::CRC32 hash;
						CryptoPP::FileSource(*p_output, true,
								new CryptoPP::HashFilter(hash,
										new CryptoPP::ArraySink((byte *)&crc32, CryptoPP::CRC32::DIGESTSIZE)));

						LogVerbose("crc32: " << RML_HEX(8, crc32) << RML_HEX_RESET << " file: "<<p_output->get_filename());

						if(crc32 != ph.crc32)
							throw rml::exception("CRC mismatch");
					}

					LogMessage("Successfully converted");

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
						p_tmp = p_output;
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
					const std::string& key(cli_confMap["encryption-key"].as<std::string>());
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

				default:
					throw rml::exception("Invalid state");

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
