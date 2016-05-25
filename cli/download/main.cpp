#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1

#include "archived.hpp"
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

#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstddef>

int main(int argc, char* argv[])
{
	try
	{
		rml::logger::configure();

		boost::program_options::variables_map cli_confMap;
		{
			boost::program_options::options_description cli_optsDesc(
					"download - download file from archive"
					"\nOptions", 100, 50);
			cli_optsDesc.add_options()
					("identifier", boost::program_options::value<std::string>(),
							"Download the object by identifier")
					("output", boost::program_options::value<std::string>(),
							"Output; Path to output file on local filesystem")
					("archived-host", boost::program_options::value<std::string>(),
							"ArchiveD hostname OR URL")
					("access-reason", boost::program_options::value<std::string>(),
							"Reason for access to the onbject from ArchiveD")
					("api-host", boost::program_options::value<std::string>(),
							"Core API hostname OR URL")
					("api-token", boost::program_options::value<std::string>(),
							"Core API token")
					("help", "this help")
					("version", "shows version information")
					("temp-dir", boost::program_options::value<std::string>()->default_value("/tmp"),
							"Temporary files will be put here")
					("log.syslog-tag", boost::program_options::value<std::string>(),
							"syslog tag to be used ( relevant for rsyslog filter )")
					;
			rml::logger::describe_config(cli_optsDesc);

			boost::program_options::store(boost::program_options::parse_command_line(argc, argv, cli_optsDesc), cli_confMap);
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

		check_cli_option(cli_confMap, "identifier", "Identifier not given");
		check_cli_option(cli_confMap, "output", "Output path not given");
		check_cli_option(cli_confMap, "archived-host", "Archived host not given");
		check_cli_option(cli_confMap, "api-host", "API host not given");
		check_cli_option(cli_confMap, "api-token", "API token not given");
		check_cli_option(cli_confMap, "access-reason", "Archive access-reason not given");

		const std::string& temp_dir(cli_confMap["temp-dir"].as<std::string>());

		const std::string& identifier(cli_confMap["identifier"].as<std::string>());

		rml::utils::tmp_fstream::pointer p_tmp_source = rml::utils::tmp_fstream::create(
				boost::replace_all_copy(identifier, "/", "_"), std::size_t(0), temp_dir);

		boost::shared_ptr< boost::asio::io_service > p_io_service = boost::make_shared< boost::asio::io_service >();
		boost::asio::io_service::work work(*p_io_service); // make sure we keep it proped up!

		app::archived archived(
				p_io_service,
				cli_confMap["archived-host"].as<std::string>(),
				cli_confMap["api-host"].as<std::string>(),
				boost::str(boost::format("ArchivingCLI:%1%.%2%") % version_number[2] % version_number[3]));

		archived.get(identifier, cli_confMap["api-token"].as<std::string>(), p_tmp_source,
				cli_confMap["access-reason"].as<std::string>());

		p_tmp_source->quick_copy(cli_confMap["output"].as<std::string>());
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
