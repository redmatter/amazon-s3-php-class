/*
 * main.cpp
 *
 *  Created on: Apr 28, 2010
 *      Author: dkorah
 */

#include <rml/logger.h>
#include <rml/utils.h>
#include <rml/config.h>
#include <rml/version.h>
#include <rml/coreapi.hpp>
#include <rml/hapi.hpp>

#include <cryptopp/md5.h>

#include "../common/types.hpp"
#include "../common/logstack.h"
#include "../common/policy.hpp"

#include <boost/system/error_code.hpp>
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/ref.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.

#include <iostream>
#include <fstream>
#include <streambuf>

#include <string.h>
#include <time.h>

#include <pugixml/pugixml.hpp>

#ifndef SYSLOG_APP_NAME
#define SYSLOG_APP_NAME "eml2a"
#endif

#ifndef DEFAULT_APP_NAME
#define DEFAULT_APP_NAME "eml2a"
#endif

#ifndef DEFAULT_APP_LONG_NAME
#define DEFAULT_APP_LONG_NAME DEFAULT_APP_NAME
#endif

#ifndef CONFIG_FILE_PATH
#define CONFIG_FILE_PATH "/etc/archiving/"DEFAULT_APP_NAME".conf"
#endif

namespace app 
{
	std::string hostname;
	std::string filename;
	boost::program_options::variables_map confMap;
	volatile bool quit = false;
	typedef enum { is_sms, is_mms } message_type_t;
	message_type_t message_type;
}

using boost::asio::ip::tcp;

typedef std::map<std::string, std::string> stringmap;
typedef std::vector<std::string> stringvec;
typedef std::map<std::string, stringvec> stringmapvec;

/*********************************************************************/
/* maintaining state for retries                                     */
/*********************************************************************/

#include <db.h>

#define STATE_NONE 0
#define STATE_FROM_ARCHIVE 1
#define STATE_FROM_INDEX 2
#define STATE_TO_ARCHIVE 3
#define STATE_TO_INDEX 4
#define STATE_DB_NAME "/var/lib/eml2a/eml2astate.db"

int load_ini_state(std::string MessageID)
{
	DB* dbp;
	int ret;
	int state_val;

	if ((ret = db_create(&dbp, NULL, 0)) != 0)
	{
		THROW_RML_EXCEPTION ("db_create: " << db_strerror(ret));
	}

	try
	{
		if ((ret = dbp->open(dbp,
			NULL, STATE_DB_NAME, NULL, DB_BTREE, DB_CREATE, 0664)) != 0)
		{
			THROW_RML_EXCEPTION ("bdb failed to open state db: " << db_strerror(ret));
		}

		DBT key, data;

		memset(&key, 0, sizeof(key));
		memset(&data, 0, sizeof(data));
		key.data = const_cast<char*>(MessageID.c_str());
		key.size = MessageID.length();

		if ((ret = dbp->get(dbp, NULL, &key, &data, 0)) == 0)
		{
			state_val = boost::lexical_cast<int>(std::string((char *)data.data, data.size));
			LogDebug ("bdb get: " << MessageID << " = " << state_val);
		}
		else
		{
			LogDebug ("bdb get failed: " << MessageID << ": " <<  db_strerror(ret));
			state_val = STATE_NONE;
		}
	}
	catch(...)
	{
		if ((ret = dbp->close(dbp, 0)) != 0)
			LogError ("bdb failed to close state db: " << db_strerror(ret));
		throw;
	}

	if ((ret = dbp->close(dbp, 0)) != 0)
		LogError ("bdb failed to close state db: " << db_strerror(ret));

	return state_val;
}

void update_ini_state(std::string MessageID, int state)
{
	DB* dbp;
	int ret;

	if ((ret = db_create(&dbp, NULL, 0)) != 0)
	{
		THROW_RML_EXCEPTION ("db_create: " << db_strerror(ret));
	}

	try
	{
		if ((ret = dbp->open(dbp,
			NULL, STATE_DB_NAME, NULL, DB_BTREE, DB_CREATE, 0664)) != 0)
		{
			THROW_RML_EXCEPTION ("bdb failed to open state db: " << db_strerror(ret));
		}

		DBT key, data;
		std::string datastr = boost::lexical_cast<std::string>(state);
		memset(&key, 0, sizeof(key));
		memset(&data, 0, sizeof(data));
		key.data = const_cast<char*>(MessageID.c_str());
		key.size = MessageID.length();
		data.data = const_cast<char*>(datastr.c_str());
		data.size = datastr.length();

		if ((ret = dbp->put(dbp, NULL, &key, &data, 0)) == 0)
			LogDebug ("db put: " << MessageID << " = " << datastr);
		else {
			THROW_RML_EXCEPTION ("bdb failed to put state into db: " << db_strerror(ret));
		}
	}
	catch(...)
	{
		if ((ret = dbp->close(dbp, 0)) != 0)
			LogError ("bdb failed to close state db: " << db_strerror(ret));
		throw;
	}

	if ((ret = dbp->close(dbp, 0)) != 0)
		LogError ("bdb failed to close state db: " << db_strerror(ret));
}

void delete_ini_state(std::string MessageID)
{
	DB* dbp;
	int ret;

	if ((ret = db_create(&dbp, NULL, 0)) != 0)
	{
		THROW_RML_EXCEPTION ("db_create: " << db_strerror(ret));
	}

	try
	{
		if ((ret = dbp->open(dbp,
			NULL, STATE_DB_NAME, NULL, DB_BTREE, DB_CREATE, 0664)) != 0)
		{
			THROW_RML_EXCEPTION ("bdb failed to open state db: " << db_strerror(ret));
		}

		DBT key;

		memset(&key, 0, sizeof(key));
		key.data = const_cast<char*>(MessageID.c_str());
		key.size = MessageID.length();

		if ((ret = dbp->del(dbp, NULL, &key, 0)) == 0)
			LogDebug("db: deleted " << MessageID);
		else
		{
			LogError ("bdb failed to delete state from db: " << db_strerror(ret));
			if (ret != DB_NOTFOUND)
				throw rml::exception("Unable to delete state from state db");
		}
	}
	catch(...)
	{
		if ((ret = dbp->close(dbp, 0)) != 0)
			LogError ("bdb failed to close state db: " << db_strerror(ret));
		throw;
	}

	if ((ret = dbp->close(dbp, 0)) != 0)
		LogError ("bdb failed to close state db: " << db_strerror(ret));
}

/*********************************************************************/
/* mime decoder for SMSs                                             */
/*********************************************************************/

#include "mime/decode.h"

class sms_content_decoder: public mime::decoder
{
public:
	sms_content_decoder(): in_data(false) {}
	virtual ~sms_content_decoder() {}
	const std::string &run (const std::string &message);
	const std::string &get_raw () { return raw; }
	virtual void object_created(mime::object* object);
	virtual void data_start(mime::object* obj);
	virtual void data(mime::object* obj, unsigned char *data, int len);
	virtual void data_end(mime::object* obj);
private:
	std::string content;
	std::string raw;
	bool in_data;
};

const std::string &sms_content_decoder::run(const std::string &message)
{
	size_t len = message.length();
	const char *p = message.c_str();
	while (len > 0)
	{
		decode(*p);
		if (in_data)
			raw += *p;
		--len;
		++p;
	}
	close();
	return content;
}

void sms_content_decoder::object_created(mime::object* object)
{
	LogDebug("MIME section: " << object->type << "/" << object->subtype);
}

void sms_content_decoder::data_start(mime::object* object)
{
	in_data = true;
}

void sms_content_decoder::data(mime::object* object, unsigned char *data, int len)
{
	for (int i = 0; i < len; i++)
		content += data[i];
}

void sms_content_decoder::data_end(mime::object* object)
{
	in_data = false;
	LogDebug("SMS content: " << content);
}

/*********************************************************************/
/* XML manipulation                                                   */
/*********************************************************************/

void XMLescape(std::string &data)
{
	using boost::algorithm::replace_all;
	replace_all(data, "&",  "&amp;");
	replace_all(data, "\"", "&quot;");
	replace_all(data, "\'", "&apos;");
	replace_all(data, "<",  "&lt;");
	replace_all(data, ">",  "&gt;");
}

std::string GenerateSMSXML(const boost::uuids::uuid &uuid, const stringmap &metadata, const std::string &FromNumber,
		const std::string &ToNumber, const std::string &Direction, const std::string &Content)
{
	// don't bother escaping - pugixml does this already apparently PLAT-1314
	//std::string escaped(Content);
	//XMLescape(escaped);

	pugi::xml_document doc;
	doc.load_buffer("<SMS/>", 6);
	pugi::xml_node smsnode = doc.child("SMS");
	smsnode.append_child("UUID").append_child(pugi::node_pcdata).set_value(boost::lexical_cast<std::string>(uuid).c_str());
	smsnode.append_child("OrgID").append_child(pugi::node_pcdata).set_value(metadata.at("OrgID").c_str());
	smsnode.append_child("UserID").append_child(pugi::node_pcdata).set_value(metadata.at("UserIDs").c_str());
	smsnode.append_child("HostName").append_child(pugi::node_pcdata).set_value(metadata.at("PBXHostName").c_str());
	smsnode.append_child("StartEpoch").append_child(pugi::node_pcdata).set_value(metadata.at("CreateTime").c_str());
	smsnode.append_child("FromNumber").append_child(pugi::node_pcdata).set_value(FromNumber.c_str());
	smsnode.append_child("ToNumber").append_child(pugi::node_pcdata).set_value(ToNumber.c_str());
	smsnode.append_child("Direction").append_child(pugi::node_pcdata).set_value(Direction.c_str());
	smsnode.append_child("Text").append_child(pugi::node_pcdata).set_value(Content.c_str());

	std::ostringstream stream;
	doc.save(stream);
	return stream.str();
}

bool DecodeAPIResponse (rml::coreapi::response &apiresponse, std::string &OrgID, std::string &UserID, const std::string &Number)
{
	if (apiresponse.first < 200 || apiresponse.first >= 300)
	{
		THROW_RML_EXCEPTION ("Error from API " << apiresponse.first << ": " << apiresponse.second);
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_buffer(apiresponse.second.c_str(), apiresponse.second.length());
	if (!result)
	{
		THROW_RML_EXCEPTION ("XML error parsing data from the API: " << result.description() << ": " << apiresponse.second);
	}
	else
	{
		pugi::xml_node SIMs_node = doc.child("SIMs");
		if (!SIMs_node)
		{
			THROW_RML_EXCEPTION ("API returned strange XML without SIMs tag: " << apiresponse.second);
		}
		else
		{
			pugi::xml_node SIM_node = SIMs_node.first_child();
			if (SIM_node)
			{
				OrgID = SIM_node.attribute("OrgID").value();
				UserID = SIM_node.attribute("UserID").value();
				if (OrgID.empty())
				{
					THROW_RML_EXCEPTION ("API returned SIM item with null OrgID: " << apiresponse.second);
				}
				LogMessage ("API got UserID " << UserID << ", OrgID " << OrgID << " for number " << Number);
				return true;
			}
			else
			{
				// else: no user with this number
				LogMessage ("API found no results for number " << Number);
			}
		}
	}
	return false;
}

/*********************************************************************/
/* Send to archive                                                   */
/*********************************************************************/

std::string remove_hyphens(const std::string &str)
{
	std::string buff(str);
	size_t hy;
	while ((hy = buff.find("-")) != std::string::npos)
		buff.erase(hy, 1);
	return buff;
}

std::string remove_redmatter(const std::string &str)
{
	std::string buff(str);
	size_t rm;
	if ((rm = buff.find(".redmatter.com")) != std::string::npos)
		buff.erase(rm);
	return buff;
}

void sendItemToArchive(const std::string &archivedHost, const std::string &archivedPort, stringmap &metadata, const std::string &uuid, const std::string &item)
{
	try
	{
		boost::asio::io_service io_service;
		tcp::socket s(io_service);
		boost::asio::streambuf inbuf;
		size_t reply_length;
		
		// lookup the archived hostname
		tcp::resolver resolver(io_service);
		tcp::resolver::query query(tcp::v4(), archivedHost.c_str(), archivedPort.c_str());
		tcp::resolver::iterator iterator = resolver.resolve(query);

		// connect to archived
		boost::asio::connect(s, iterator);
		
		// wait for archiveD signon: ArchiveD v0.1.2 protocol 1.0\r\n
		reply_length = boost::asio::read_until (s, inbuf, boost::regex("\r\n"));

		// check protocol and so on look good
		std::istream is1(&inbuf);
		std::string ArchiveD, version_number, protocol, protocol_version_number;
		char v, lf;
		if(!((std::getline(is1, ArchiveD, ' ') &&
			is1.get(v) &&
			std::getline(is1, version_number, ' ') &&
			std::getline(is1, protocol, ' ') &&
			std::getline(is1, protocol_version_number, '\r') &&
			is1.get(lf)) &&
			ArchiveD == "ArchiveD" && protocol == "protocol")) 
			throw rml::exception("malformed archived signon");
		else if(protocol_version_number != "1.0")
			throw rml::exception("archived protocol mismatch");
		else if(version_number != version_number_only_string)
			LogWarning("Warning: Server differs in version from client: " << version_number << ":" << version_number_only_string);
		
		// Send eml2a signon string: OK CollectD va.b.c protocol 1.0\r\n
		std::string signon = std::string("OK CollectD v")+version_number_only_string+" protocol 1.0\r\n";
		boost::asio::write(s, boost::asio::buffer(signon));

		// wait for archiveD OK response: OK\r\n
		reply_length = boost::asio::read_until (s, inbuf, boost::regex("\r\n"));

		// check we got OK
		std::string OkOrErr;
		std::istream is2(&inbuf);
		std::getline(is2, OkOrErr, ' ');
		if (OkOrErr != "OK\r\n")
			throw rml::exception("archived returned ERR to signon");

		// generate the PUT command
		std::ostringstream PUTstr;
		PUTstr << "PUT " << metadata["FileType"] << " " << metadata["OrgID"] << " " << uuid << " " << item.length() << "\r\n";
		stringmap::iterator i;
		for(i = metadata.begin(); i != metadata.end(); ++i)
			PUTstr << i->first << ": " << i->second << "\r\n";
		PUTstr << "\r\n";
		LogDebug(PUTstr.str());
		boost::asio::write(s, boost::asio::buffer(PUTstr.str()));
		
		// wait for archiveD response to PUT: OK PUT <Type> <OrgID> <UUID>\r\n
		reply_length = boost::asio::read_until (s, inbuf, boost::regex("\r\n"));

		// check the response looks good
		std::istream is3(&inbuf);
		std::string rOK, rVerb, rType, rOrgID, rUUID;
		char rLF;
		if(!((std::getline(is3, rOK, ' ') &&
			std::getline(is3, rVerb, ' ') &&
			std::getline(is3, rType, ' ') &&
			std::getline(is3, rOrgID, ' ') &&
			std::getline(is3, rUUID, '\r') &&
			is3.get(rLF)) &&
			rOK == "OK" && rVerb == "PUT" && rType == metadata["FileType"] && rOrgID == metadata["OrgID"] && uuid == rUUID)) 
		{
			THROW_RML_EXCEPTION("archived failed the put command: " << rOK << " " << rVerb << " " << rType << " " << rOrgID << " " << rUUID);
		}

		// send the data followed by ctrl-z
		boost::asio::write(s, boost::asio::buffer(item));
		boost::asio::write(s, boost::asio::buffer(std::string("\x1a")));

		// wait for archiveD response to file: OK RECV <Type> <OrgID> <UUID>\r\n
		reply_length = boost::asio::read_until (s, inbuf, boost::regex("\r\n"));

		// check the response looks good
		std::istream is4(&inbuf);
		std::string fOK, fVerb, fType, fOrgID, fUUID;
		if(!((std::getline(is4, fOK, ' ') &&
			std::getline(is4, fVerb, ' ') &&
			std::getline(is4, fType, ' ') &&
			std::getline(is4, fOrgID, ' ') &&
			std::getline(is4, fUUID, '\r')) &&
			fOK == "OK" && fVerb == "RECV" && fType == metadata["FileType"] && fOrgID == metadata["OrgID"] && uuid == fUUID))
		{
			THROW_RML_EXCEPTION("archived failed receiving the file: " << fOK << " " << fVerb << " " << fType << " " << fOrgID << " " << fUUID);
		}

		// if we are here, we're good!
		LogMessage(uuid << " sent to archive ok");

		// send BYE\r\n\r\n and wait for OK\r\n - fixes PLAT-1417
		boost::asio::write(s, boost::asio::buffer(std::string("BYE\r\n\r\n")));
		boost::asio::read_until (s, inbuf, boost::regex("\r\n"));
	}
	catch (const boost::system::error_code& err)
	{
		THROW_RML_EXCEPTION("Boost library exception while sending item to archive: "<<err);
	}	
	catch (const std::exception& e)
	{
		THROW_RML_EXCEPTION("Exception while sending item to archive: "<<e.what());
	}
}

std::string MD5(const std::string &data)
{
	CryptoPP::Weak1::MD5 hash;
	byte digest[ CryptoPP::Weak1::MD5::DIGESTSIZE ];
	 
	hash.CalculateDigest (digest, (byte*) data.c_str(), data.length());
	
	CryptoPP::HexEncoder encoder;
	std::string output;
	encoder.Attach (new CryptoPP::StringSink (output));
	encoder.Put (digest, sizeof(digest));
	encoder.MessageEnd ();
	
	return output;
}

bool StringToBool(const std::string &str)
{
	if (boost::iequals(str, "true"))
		return true;
	else if (boost::iequals(str, "1"))
		return true;
	else if (boost::iequals(str, "yes"))
		return true;
	else if (boost::iequals(str, "false"))
		return false;
	else if (boost::iequals(str, "0"))
		return false;
	else if (boost::iequals(str, "no"))
		return false;

	THROW_RML_EXCEPTION("Unable to interpret string as bool (true/false/1/0/yes/no): " << str);
}

void Configure(int argc, char* argv[])
{
	// default initialization to catch any initialization error
	rml::logger::configure();

	// initialise host cache for performing hapi substitution in config files
	rml::hapi::host_cache::initialise();

	boost::program_options::variables_map cli_confMap;
	{
		boost::program_options::options_description cli_optsDesc(
			boost::str(boost::format(
				DEFAULT_APP_LONG_NAME" v%1%"
				"\n%2%\n\nOptions"
				) % version_number_string % copyright_string));
		cli_optsDesc.add_options()
				("hostname", boost::program_options::value<std::string>(), "this hostname")
				("sms", "it's an sms")
				("mms", "it's an mms")
				("file", boost::program_options::value<std::string>(), "file to load - will be deleted on success")
				("help-conf", "conf file help")
				("help", "this help")
				("version", "shows version information")
				;
		boost::program_options::store(boost::program_options::parse_command_line(argc, argv, cli_optsDesc), cli_confMap);
		boost::program_options::notify(cli_confMap);

		if (cli_confMap.count("help")) {
			std::cout << cli_optsDesc << "\n";
			exit(rml::utils::exit_codes::normal);
		}
		
		if(cli_confMap.count("version")) {
			std::cout << boost::str( boost::format(DEFAULT_APP_LONG_NAME" v%1%\n%2%\n") % version_number_string % copyright_string );
			exit(rml::utils::exit_codes::normal);
		}
	}

	boost::program_options::options_description optsDesc(boost::str( boost::format(DEFAULT_APP_LONG_NAME" v%1%\n%2%\noptions") % version_number_string % copyright_string ));
	optsDesc.add_options()
		("archived_server_host", boost::program_options::value<std::string>()->default_value(std::string("archived.redmatter.com")), "archive daemon host")
		("archived_server_port", boost::program_options::value<std::string>()->default_value(std::string("10100")), "archive daemon port number")
		;

	rml::logger::describe_config(optsDesc);
	rml::coreapi::describe_config(optsDesc);

	if(cli_confMap.count("help-conf")) {
		std::cout << optsDesc << "\n";
		exit(rml::utils::exit_codes::normal);
	}

	if (cli_confMap.count("hostname") != 1)
		throw rml::exception("hostname not given");

	if (cli_confMap.count("sms") == 1)
		app::message_type = app::is_sms;
	else if (cli_confMap.count("mms") == 1)
		app::message_type = app::is_mms;
	else
		throw rml::exception("Neither mms nor sms specified, must have one or the other.");

	app::hostname = cli_confMap["hostname"].as<std::string>();
	if (cli_confMap.count("file") != 1)
		app::filename = cli_confMap["file"].as<std::string>();

	try {
		rml::config::parse(CONFIG_FILE_PATH, optsDesc, app::confMap);

		// configure the logger
		rml::logger::configure(app::confMap, SYSLOG_APP_NAME);

		// a new day is a gift!
		LogAlways(DEFAULT_APP_LONG_NAME" v"<<version_number_string<<' '<<copyright_string);
		LogAlways("built on "<<version_date<<' '<<build_host);
	}
	catch(const std::exception &e) {
		LogError("Initialization Error - exception: "<<e.what());
		exit(rml::utils::exit_codes::initialization_error);
	}
}

std::string GetFromHeader(const std::string &header, const boost::regex &number_from_header_regex)
{
	boost::cmatch what;

	if (regex_match (header.c_str(), what, number_from_header_regex))
	{
		// what[0] contains the whole string
		// what[1] contains the number
		LogMessage("Extracted number " << what[1] << " from " << header);
		return std::string(what[1]);
	}

	THROW_RML_EXCEPTION("Unable to extract number from header: " << header);
}

void ProcessEmailHeaders(std::string message, stringmapvec &headers)
{
	size_t pos;

	// process headers
	size_t start = 0;
	std::string header;
	while ((pos = message.find("\n", start)) != std::string::npos)
	{
		std::string raw(message.substr(start, pos - start));

		// note - line ending will be either \n or \r\n
		// we searched for \n; check there's no dangling \r at the
		// end of the line
		size_t raw_len = raw.length();
		if (raw_len > 0)
		{
			if (raw.at(raw_len-1) == '\r')
				raw.erase(raw_len-1);
		}

		start = pos + 1;

		if (raw.empty()) // end of headers?
			break;

		if(raw.at(0) == ' ' || raw.at(0) == '\t')
		{
			if (header.empty() || headers[header].empty())
			{
				THROW_RML_EXCEPTION ("Error processing message continuation header: not a continuation: " << raw);
			}

			// continuation of previous header
			std::string value = headers[header].back() + " " + boost::algorithm::trim_copy(raw);

			headers[header].back() = value;
			LogDebug("Header (cont): " << header << ": " << value);
		}
		else
		{
			pos = raw.find(":");
			if (pos == std::string::npos)
			{
				THROW_RML_EXCEPTION ("Error processing message headers: strangely written header: " << raw);
			}

			header = boost::algorithm::trim_copy(raw.substr(0, pos));
			std::string value  = boost::algorithm::trim_copy(raw.substr(pos + 1));

			headers[header].push_back(value);
			LogDebug("Header: " << header << ": " << value);
		}
	}

	//body = message.substr(start);
}

int main(int argc, char* argv[]) try
{
	Configure(argc, argv);

	// get the current time
	time_t timenow = time(NULL);

	// set up connection to coreapi
	boost::shared_ptr< boost::asio::io_service > p_io_service = boost::make_shared< boost::asio::io_service >();
	rml::coreapi coreapi(app::confMap, boost::str(boost::format("%1%:%2%.%3%") % DEFAULT_APP_NAME % version_number[2] % version_number[3]), p_io_service);
	rml::coreapi::response apiresponse;

	// read contents of stdin
	std::string message;
	if (app::filename.empty())
	{
		LogMessage ("Loading content from stdin");
		message = static_cast<std::stringstream const&>(std::stringstream() << std::cin.rdbuf()).str();
	}
	else
	{
		LogMessage ("Loading content from " << app::filename);
		std::ifstream t(app::filename.c_str());
		message = std::string((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
	}

	// process the email into headers and body
	stringmapvec headers;
	ProcessEmailHeaders(message, headers);

	if (headers.find("Message-ID") == headers.end())
	{
		THROW_RML_EXCEPTION ("No Message-ID header - looks like the wrong kind of file");
	}

	// do MMS/SMS-specific things
	std::string decoded_body;
	std::string FromNumber;
	std::string ToNumber;
	std::string MessageID = headers["Message-ID"].back();

	// get the current ini state value, if any, so we don't
	// reprocess anything on retry
	int saved_ini_state = load_ini_state(MessageID);
	int current_ini_state = STATE_NONE;

	if (app::message_type == app::is_sms)
	{
		// note - this currently covers MT SMS only! We don't have a sample of an MO SMS
		// SMS: the From header
		// From: <4793859166@sms.netcom.no>
		FromNumber = GetFromHeader(headers["From"].back(), boost::regex("[^<]*<([0-9]+)@.*"));
		// Subject: NetCom SMSpluss - Kopi av SMS til 4793224070
		ToNumber   = GetFromHeader(headers["Subject"].back(), boost::regex("[^0-9]*([0-9]+)"));

		sms_content_decoder sms_decoder;
		decoded_body = sms_decoder.run(message);
	}
	else
	{
		// MMS: the From and To headers contain our numbers, eg
		// From: <4793224070@mms.netcom.no>
		// To: <4799324091@mms.netcom.no>
		FromNumber = GetFromHeader(headers["From"].back(), boost::regex("[^<]*<([0-9]+)@.*"));
		ToNumber   = GetFromHeader(headers["To"].back(), boost::regex("[^<]*<([0-9]+)@.*"));
	}

	// process message twice: once for 'from', once for 'to'
	typedef enum { from_number, to_number, done } state_t;
	state_t state;
	bool FromUserNotFound = true;
	bool ToUserNotFound = true;

	for (state = from_number; state != done; state = static_cast<state_t>(static_cast<int>(state)+1))
	{
		std::string direction;
		std::string apicall;
		std::string UserID, OrgID;

		// look up numbers in API

		if (state == from_number)
		{
			apicall = std::string("/newmobile/bynumber/") + FromNumber;
			apiresponse = coreapi.get(apicall.c_str());
			if (!DecodeAPIResponse (apiresponse, OrgID, UserID, FromNumber))
				continue;
			FromUserNotFound = false;
			direction = "Outbound";
			current_ini_state = STATE_FROM_ARCHIVE;
		}
		else if (state == to_number)
		{
			apicall = std::string("/newmobile/bynumber/") + ToNumber;
			apiresponse = coreapi.get(apicall.c_str());
			if (!DecodeAPIResponse (apiresponse, OrgID, UserID, ToNumber))
				continue;
			ToUserNotFound = false;
			direction = "Inbound";
			current_ini_state = STATE_TO_ARCHIVE;
		}

		// note: a different UUID for 'from' and 'to' users
		boost::uuids::uuid uuid = boost::uuids::random_generator()();

		if (current_ini_state > saved_ini_state)
		{
			update_ini_state(MessageID, current_ini_state);

			// load the org's sms retention policy (we use sms retention policy for mms too)
			app::policy::cached_finder_t policyCache(boost::bind(&app::policy::finder, coreapi, _1), "PolicyCache");
			app::policy::pointer thepolicy = policyCache(std::make_pair(boost::lexical_cast<int>(OrgID), app::policy::id_type(app::policy::pseudo_id::sms)));
			if (!thepolicy)
				throw rml::exception("Error trying to get the policy from the core API");

			stringmap metadata;
			metadata["OrgID"] 				= OrgID;
			metadata["UserIDs"] 			= UserID;
			metadata["FileType"] 			= "sms";
			metadata["RetentionPolicyID"] 	= boost::lexical_cast<std::string>(thepolicy->id);
	//		metadata["MD5"] 				= MD5(item);
			metadata["CreateTime"] 			= boost::lexical_cast<std::string>(timenow);
			metadata["LegUUID"] 			= boost::lexical_cast<std::string>(uuid);
	//		metadata["OriginatorUUID"] 		= "";
			metadata["DataFormat"] 			= app::message_type == app::is_sms ? "xml" : "email";
			metadata["PBXHostName"] 		= app::hostname;
	//		metadata["AddToCDRMunchCache"] 	= "";

			std::string host = app::confMap["archived_server_host"].as<std::string>();
			std::string port = app::confMap["archived_server_port"].as<std::string>();

			std::string smsxml;

			// if it's an SMS make an XML doc for the SMS archive
			if (app::message_type == app::is_sms)
			{
				smsxml = GenerateSMSXML(uuid, metadata, FromNumber, ToNumber, direction, decoded_body);
				LogDebug("SMS XML: " << smsxml);
			}

			std::string &item = app::message_type == app::is_sms ? smsxml : message;

			metadata["MD5"] = MD5(item);

			sendItemToArchive(host, port, metadata, boost::lexical_cast<std::string>(uuid), item);
		}
		else
			LogMessage("Not sending to archive, already done");

		current_ini_state ++;

		if (current_ini_state > saved_ini_state)
		{
			update_ini_state(MessageID, current_ini_state);

			// add to the index
			std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Messages>\n";
			xml += boost::str (boost::format ("<SMS UUID=\"%1%\" OrgID=\"%2%\" UserID=\"%3%\" Time=\"%4%\" From_Number=\"%5%\" To_Number=\"%6%\" Direction=\"%7%\" HostName=\"%8%\"/>\n")
				% remove_hyphens(boost::lexical_cast<std::string>(uuid))
				% OrgID
				% UserID
				% timenow
				% FromNumber
				% ToNumber
				% direction
				% remove_redmatter(app::hostname)
			);
			xml += "</Messages>";

			apiresponse = coreapi.post("/smslogs/push", xml);
			if (apiresponse.first < 200 || apiresponse.first >= 300)
			{
				THROW_RML_EXCEPTION ("Error adding SMS " << uuid << " to index: " << apiresponse.first << ", " << apiresponse.second);
			}
			else
				LogMessage (uuid << " added to index ok");
		}
		else
			LogMessage("Not sending to index, already done");
	}

	// throw an exception if no valid users
	if (FromUserNotFound && ToUserNotFound)
	{
		THROW_RML_EXCEPTION ("Unable to map either " << FromNumber << " or " << ToNumber << " to users on the system - exiting with error");
	}

	// if we are here there were no exceptions and we can delete
	// the state setting in the ini file
	delete_ini_state(MessageID);

	// if we were given the filename on the command line, and we were successful,
	// delete it.
	if (!app::filename.empty())
	{
		LogMessage ("Deleting file " << app::filename);
		unlink(app::filename.c_str());
	}

	LogMessage("Program terminated normally");
	exit(rml::utils::exit_codes::normal);
}
catch(const std::exception& e) {
	LogError("Exception: "<<e.what());
	LogMessage("Abnormal program termination");
	exit(rml::utils::exit_codes::failure);
}
catch(...) {
	LogError("Unexpected exception");
	LogMessage("Abnormal program termination");
	exit(rml::utils::exit_codes::failure);
}
