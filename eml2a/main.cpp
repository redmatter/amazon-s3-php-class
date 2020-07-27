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

#include "../common/types.hpp"
#include "../common/logstack.h"
#include "../common/policy.hpp"
#include "../common/hostname.hpp"

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
#include <boost/asio/ip/host_name.hpp>    // get host name
#include <boost/date_time.hpp>

#include <iostream>
#include <fstream>
#include <streambuf>

#include <string.h>
#include <time.h>

#include <pugixml/pugixml.hpp>

#ifndef SYSLOG_APP_NAME
#define SYSLOG_APP_NAME "rm-eml2a"
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
	typedef enum { is_both, is_mo, is_mt } direction_t;
	message_type_t message_type;
	direction_t direction;
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
#define STATE_TO_ARCHIVE 2
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

bool DecodeAPIXAuthResponse (rml::coreapi::response &apiresponse, std::string &Token)
{
	if (apiresponse.first != 200)
	{
		THROW_RML_EXCEPTION ("Error from /x/auth " << apiresponse.first << ": " << apiresponse.second);
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_buffer(apiresponse.second.c_str(), apiresponse.second.length());
	if (!result)
	{
		THROW_RML_EXCEPTION ("XML error parsing data from the API: " << result.description() << ": " << apiresponse.second);
	}
	else
	{
		pugi::xml_node auth_node = doc.child("Auth");
		if (!auth_node)
		{
			THROW_RML_EXCEPTION ("API returned strange XML without Auth tag: " << apiresponse.second);
		}
		else
		{
			Token = auth_node.attribute("Token").value();
			if (Token.empty())
			{
				THROW_RML_EXCEPTION ("API returned auth item with null Token: " << apiresponse.second);
			}
			LogMessage ("API got Token " << Token);
			return true;
		}
	}
	return false;
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
				("mo", "it's an MO message [default is MO+MT]")
				("mt", "it's an MT message [default is MO+MT]")
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
		("content_size_limit", boost::program_options::value<size_t>()->default_value(4*1024*1024), "maximum allowed content length")
		;

	rml::logger::describe_config(optsDesc);
	rml::coreapi::describe_config(optsDesc);

	if(cli_confMap.count("help-conf")) {
		std::cout << optsDesc << "\n";
		exit(rml::utils::exit_codes::normal);
	}

	if (cli_confMap.count("sms") == 1)
		app::message_type = app::is_sms;
	else if (cli_confMap.count("mms") == 1)
		app::message_type = app::is_mms;
	else
		throw rml::exception("Neither mms nor sms specified, must have one or the other.");

	if (cli_confMap.count("mo") == 1)
		app::direction = app::is_mo;
	else if (cli_confMap.count("mt") == 1)
		app::direction = app::is_mt;
	else
		app::direction = app::is_both;

	if(cli_confMap.count("hostname")) {
		app::hostname = cli_confMap["hostname"].as<std::string>();
	} else {
		app::hostname = archiving::common::get_hostname();
	}
	if (cli_confMap.count("file") == 1)
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

	if (regex_search (header.c_str(), what, number_from_header_regex))
	{
		// what[0] contains the whole string
		// what[1] contains the number
		LogMessage("Extracted number " << what[1] << " from " << header);
		return std::string(what[1]);
	}

	LogMessage("Unable to extract number from header: " << header << ", returning empty string");
	return std::string();
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

	std::string agent = boost::str(boost::format("%1%:%2%.%3%") % DEFAULT_APP_NAME % version_number[2] % version_number[3]);

	// set up connection to coreapi

	std::string coreapi_url = app::confMap["coreapi.url"].as<std::string>();
	std::string coreapi_host = coreapi_url.substr(coreapi_url.find_last_of('/') + 1);

	boost::shared_ptr< boost::asio::io_service > p_io_service = boost::make_shared< boost::asio::io_service >();
	rml::coreapi coreapi(coreapi_url, agent, p_io_service);
	rml::coreapi::response apiresponse;

	rml::coreapi::headers apiheaders;
	apiheaders.insert(std::make_pair("Host", coreapi_host.c_str()));
	apiheaders.insert(std::make_pair("User-Agent", agent.c_str()));

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
		message.assign((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
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
	std::string FromNumber, FromNumberFallback;
	std::string ToNumber, ToNumberFallback;
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
		FromNumber = GetFromHeader(headers["From"].back(), boost::regex("([0-9]+)@.*"));
		// Subject: NetCom SMSpluss - Kopi av SMS til 4793224070
		ToNumber   = GetFromHeader(headers["Subject"].back(), boost::regex("([0-9]+)$"));

		// what will be used if we can't extract numbersf
		FromNumberFallback = FromNumber.empty() ? headers["From"].back().substr(0, 18) : FromNumber;
		ToNumberFallback   = ToNumber.empty() ? "unknown" : ToNumber;

		sms_content_decoder sms_decoder;
		decoded_body = sms_decoder.run(message);
	}
	else
	{
		// MMS: the From and To headers contain our numbers, eg
		// From: <4793224070@mms.netcom.no>
		// To: <4799324091@mms.netcom.no>
		FromNumber = GetFromHeader(headers["From"].back(), boost::regex("([0-9]+)@.*"));
		ToNumber   = GetFromHeader(headers["To"].back(), boost::regex("([0-9]+)@.*"));

		// what will be used if we can't extract numbers
		FromNumberFallback = FromNumber.empty() ? headers["From"].back().substr(0, 18) : FromNumber;
		ToNumberFallback   = ToNumber.empty() ? headers["To"].back().substr(0, 18) : ToNumber;
	}

	XMLescape(FromNumberFallback);
	XMLescape(ToNumberFallback);

	// get the content, depending on type, and look for ]]> sequences. These is the XML
	// CDATA end tag, and if it occurs in the body, it needs to be split by a 'real' XML
	// start/end tag
	// http://stackoverflow.com/questions/223652/is-there-a-way-to-escape-a-cdata-end-token-in-xml

	std::string msgdata = app::message_type == app::is_sms ? decoded_body : message;
	boost::replace_all(msgdata, "]]>", "]]]]><![CDATA[>");

	if (msgdata.length() > app::confMap["content_size_limit"].as<size_t>())
		THROW_RML_EXCEPTION ("Message data length exceeds limit");

	// get Date header value in UTC

	time_t since_epoch = 0;
	std::string since_epoch_str;

	// Thu, 5 Feb 2015 08:24:15 +0000 ... Wed, 22 Apr 2015 13:42:08 +0200,
	boost::regex DateHdrRegex("([A-Z][a-z]{2}, [0-9]{1,2} [A-Z][a-z]{2} [0-9]{4} [0-9]{2}:[0-9]{2}:[0-9]{2}) ([+-]{0,1}[0-9]{4})");

	boost::cmatch what;
	std::string DateHdrString = headers["Date"].back();
	if (!DateHdrString.empty() && regex_search (DateHdrString.c_str(), what, DateHdrRegex))
	{
		// what[0] contains the whole string
		// what[1] contains the date/time without time zone
		// what[2] contains the time zone offset

		std::locale date_loc(std::locale::classic(), new boost::local_time::local_time_input_facet("%a, %d %b %Y %H:%M:%S"));
		boost::local_time::time_zone_ptr tz_utc(new boost::local_time::posix_time_zone("UTC"));
		boost::local_time::local_date_time DateTimeNow = boost::local_time::local_sec_clock::local_time(tz_utc);

		boost::local_time::local_date_time DateHdrTime = DateTimeNow;
		std::string date_str = std::string(what[1]);

		// insert a leading zero in the day of the month, if necessary

		// this fires if we need to insert a 0 before the day of the month
		boost::regex DateStrRegex("[A-Z][a-z]{2}, ([1-9]) [A-Z][a-z]{2} [0-9]{4} [0-9]{2}:[0-9]{2}:[0-9]{2}");
		boost::cmatch what2;
		if (regex_search (date_str.c_str(), what2, DateStrRegex))
		{
			date_str.insert(5, "0", 1);
			LogVerbose ("Inserted 0: " << what[1] << " -> " << date_str);
		}

		try
		{
			std::istringstream DateStringStream(date_str);
			DateStringStream.exceptions(std::ios_base::failbit);
			DateStringStream.imbue(date_loc);
			DateStringStream >> DateHdrTime;

			// only do the time zone conversion if there was no exception mapping to UTC

			unsigned long tz_mins, tz_len = what[2].length();
			std::string tz_str = std::string(what[2]);
			const char *tz_cstr = tz_str.c_str();
			tz_mins  = (tz_cstr[tz_len-1]-'0') + (tz_cstr[tz_len-2]-'0')*10 + (tz_cstr[tz_len-3]-'0')*60 + (tz_cstr[tz_len-4]-'0')*600;
			if (tz_mins != 0)
			{
				if (tz_cstr[0] == '-')
					DateHdrTime += boost::posix_time::minutes(tz_mins);
				else
					DateHdrTime -= boost::posix_time::minutes(tz_mins);
			}

			LogDebug ("Mapped " << DateHdrString << " to UTC time " << DateHdrTime);

			boost::posix_time::ptime utc = DateHdrTime.utc_time();
			boost::posix_time::ptime epoch(boost::gregorian::date(1970, 1, 1));
			boost::posix_time::time_duration::sec_type diff = (utc - epoch).total_seconds();
			since_epoch = diff;

			LogVerbose ("Mapped " << DateHdrString << " to epoch time " << since_epoch << ", " << asctime(gmtime(&since_epoch)));
		}
		catch(const std::exception &e)
		{
			LogWarning ("Error mapping Date hdr string to UTC: " << e.what() << ". Using current time.");
			time(&since_epoch);
		}
	}
	else
		time(&since_epoch);

	since_epoch_str = boost::lexical_cast<std::string>(since_epoch);

	// auth onto core API using xauth
	std::string Token;
	rml::coreapi::headers xauthheaders;
	xauthheaders.insert(std::make_pair("Authorization", ("Basic "+app::confMap["coreapi.auth_creds"].as<std::string>()).c_str()));
	xauthheaders.insert(std::make_pair("Content-Type", "application/xml"));
	apiresponse = coreapi.get("/x/auth", false, xauthheaders);
	DecodeAPIXAuthResponse (apiresponse, Token);
	apiheaders.insert(std::make_pair("Authorization", Token.c_str()));

	// process message twice: once for 'from', once for 'to'
	typedef enum { from_number, to_number, done } state_t;
	state_t state;
	bool FromUserNotFound = true;
	bool ToUserNotFound = true;

	for (state = from_number; state != done; state = static_cast<state_t>(static_cast<int>(state)+1))
	{
		if (current_ini_state >= saved_ini_state)
		{
			update_ini_state(MessageID, current_ini_state);

			std::string apicall;

			// look up numbers in API

			if (state == from_number)
			{
				if (app::direction == app::is_mt)
					continue;

				if (FromNumber.empty())
					continue;

				pugi::xml_document doc;
				doc.load_buffer("<Data/>", 7);
				pugi::xml_node datanode = doc.child("Data");
				datanode.append_attribute("TYPE") 		= (app::message_type == app::is_sms) ? "SMS" : "MMS";
				datanode.append_attribute("DIRECTION") 	= "MO";
				datanode.append_attribute("MNO") 		= "Netcom";
				datanode.append_attribute("MSISDN") 	= FromNumber.c_str();
				datanode.append_attribute("CDPA") 		= ToNumberFallback.c_str();
				datanode.append_attribute("EPOCH") 		= since_epoch_str.c_str();

				datanode.append_child(pugi::node_cdata).set_value(msgdata.c_str());

				std::ostringstream stream;
				doc.save(stream);

				apiresponse = coreapi.post("/x/mno/smsrequest", stream.str(), "application/xml", false, apiheaders);

				if (apiresponse.first == 404)
				{
					LogMessage (FromNumber << " not found: " << apiresponse.first << ": " << apiresponse.second);
					continue;
				}
				else if (apiresponse.first < 200 || apiresponse.first >= 300)
				{
					THROW_RML_EXCEPTION ("Error from API " << apiresponse.first << ": " << apiresponse.second);
				}

				FromUserNotFound = false;
				current_ini_state = STATE_FROM_ARCHIVE;
			}
			else if (state == to_number)
			{
				if (app::direction == app::is_mo)
					continue;

				if (ToNumber.empty())
					continue;

				pugi::xml_document doc;
				doc.load_buffer("<Data/>", 7);
				pugi::xml_node datanode = doc.child("Data");
				datanode.append_attribute("TYPE") 		= (app::message_type == app::is_sms) ? "SMS" : "MMS";
				datanode.append_attribute("DIRECTION") 	= "MT";
				datanode.append_attribute("MNO") 		= "Netcom";
				datanode.append_attribute("MSISDN") 	= FromNumberFallback.c_str();
				datanode.append_attribute("CDPA") 		= ToNumber.c_str();
				datanode.append_attribute("EPOCH") 		= since_epoch_str.c_str();

				datanode.append_child(pugi::node_cdata).set_value(msgdata.c_str());

				std::ostringstream stream;
				doc.save(stream);

				apiresponse = coreapi.post("/x/mno/smsrequest", stream.str(), "application/xml", false, apiheaders);

				if (apiresponse.first == 404)
				{
					LogMessage (ToNumber << " not found: " << apiresponse.first << ": " << apiresponse.second);
					continue;
				}
				else if (apiresponse.first < 200 || apiresponse.first >= 300)
				{
					THROW_RML_EXCEPTION ("Error from API " << apiresponse.first << ": " << apiresponse.second);
				}

				ToUserNotFound = false;
				current_ini_state = STATE_TO_ARCHIVE;
			}
		}
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
