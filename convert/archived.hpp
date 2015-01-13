/*
 * archived.hpp
 *
 *  Created on: 03/01/2013
 *      Author: dkorah
 */

#ifndef ARCHIVED_HPP_
#define ARCHIVED_HPP_

#include "../archived/types.hpp"
#include "../common/types.hpp"
#include "../common/archivable.hpp"
#include "../common/coreapi_eventlog.hpp"

#include <rml/logger.h>
#include <rml/exception.h>
#include <rml/utils.h>
#include <rml/coreapi.hpp>

#include <pugixml/pugixml.hpp>

#include <boost/network/protocol/http.hpp>
#include <boost/asio.hpp>
#include <boost/iterator.hpp>
#include <boost/format.hpp>

#include <iterator>

namespace app {

class archived
{
	typedef boost::network::http::basic_client<
			boost::network::http::tags::http_async_8bit_udp_resolve , 1 , 1 > http_client;
	typedef http_client::request http_client_request;
	typedef http_client::response http_client_response;

	const std::string base_uri_;

	boost::shared_ptr< boost::asio::io_service > p_io_service_;

	http_client client_;
	rml::coreapi coreapi_;

	inline std::string get_my_ip(const bool https) const
	{
		try {
			// connect to ArchiveD and get local IP
		    boost::asio::ip::tcp::resolver resolver(*p_io_service_);
		    boost::asio::ip::tcp::resolver::query query(boost::asio::ip::tcp::v4(), base_uri_, https? "https": "http");
		    boost::asio::ip::tcp::resolver::iterator iterator = resolver.resolve(query);

		    boost::asio::ip::tcp::socket s(*p_io_service_);
		    s.connect(*iterator);

		    return s.local_endpoint().address().to_string();
		}
		catch(const std::exception& e) {
			LogWarning("Exception: "<<e.what());
		}

		return "";
	}


public:
	class exception : public rml::exception
	{
	public:
		const int status;

		exception(const int status, const std::string& status_message) : rml::exception(status_message), status(status)
		{
		}
	};

private:
	static void get_handler(bool& finished, bool& error, const rml::utils::tmp_fstream::pointer& fstream,
			boost::iterator_range<char const *> const& buffer_range, boost::system::error_code const& ec)
	{
		if(!ec) {
			LogDebug("Received "<<buffer_range.size()<<" bytes");
			boost::copy(buffer_range, std::ostream_iterator<
					boost::network::char_<http_client::request::tag>::type>(*fstream));

			if(!fstream->good()) {
				finished = true;
				error = true;
				throw exception(0, strerror(errno));
			}
		}
		else if(ec == boost::asio::error::eof) {
			LogMessage("GET successful");
			finished = true;
			error = false;
		}
		else {
			finished = true;
			error = true;
			throw exception(0, ec.message());
		}
	}

public:
	archived(const boost::shared_ptr< boost::asio::io_service >& io_service, const std::string& base_uri, const std::string& api_host, const std::string& api_ui_version) :
		base_uri_(base_uri), p_io_service_(io_service),
		coreapi_(
				boost::str(boost::format("%1%%2%") % ((api_host[4] == ':' || api_host[5] == ':')? "": "http://")  % api_host),
				api_ui_version, p_io_service_)
	{
	}

	types::file_format::file_format_t get(const std::string& key_str, const std::string& authorization, const rml::utils::tmp_fstream::pointer& fstream, const std::string& reason="", const std::string& ip="")
	{
		archiving::archivable::identifier key;
		{
			std::istringstream istr(key_str);
			istr >> key;
			LogDebug("Loading key: "<<key);
		}

		const std::string authorization_ip(ip.empty()? get_my_ip(base_uri_[5] == ':'): ip);

		coreapi_eventlog::pointer p_ev;
		{
			std::string audit_text;
			{
				std::ostringstream ostr;
				ostr << "File "<<key<<" accessed";
				if(reason.length() > 3)
					ostr << "; Reason given was \""<<reason<<'"';

				audit_text = ostr.str();
			}

			std::string extra_data;
			{
				std::ostringstream ostr;
				ostr << "{\"AuditObject\":{\"Type\":\"File\", \"Identifier\":\""<<key<<"\"}, \"Reason\":\""<<boost::algorithm::replace_all_copy(reason, "\"", "\\\"")<<"\"}";
				extra_data = ostr.str();
			}

			p_ev = coreapi_eventlog::create(coreapi_, key.org, coreapi_eventlog::levels::message, audit_text, "Archiving", "FetchObject", 0, 0, extra_data, authorization, authorization_ip);
		}

		// TODO de-fudge audit-token=999
		const std::string url(boost::str(boost::format("%1%%2%/%3%?audit-token=%4%") % ((base_uri_[4] == ':' || base_uri_[5] == ':')? "": "http://")  % base_uri_ % key_str % p_ev->id));
		LogDebug(boost::this_thread::get_id()<<" url: "<<url);

		http_client_request request(url);
		request << boost::network::header("Authorization", authorization);
		request << boost::network::header("IP", authorization_ip);

		const bool stream_was_open(fstream->is_open());
		if(!stream_was_open)
			fstream->re_open();

		bool finished(false);
		bool error(false);
		http_client_response response;
		try {
			response = client_.get(request, boost::bind(archived::get_handler, boost::ref(finished), boost::ref(error), fstream, _1, _2));
		}
		catch(const std::exception& e) {
			LogError(boost::this_thread::get_id()<<" Error while performing GET; err:"<<e.what());
			throw;
		}

		while(!finished)
			usleep(200000);

		typedef boost::network::headers_range<http_client::response>::type response_headers;
		typedef boost::range_iterator< response_headers >::type iterator;

		types::file_format::file_format_t file_format(types::file_format::unknown);
		std::string md5_etag;

		response_headers headers_ = response.headers();
		for(iterator it = headers_.begin(); it != headers_.end(); ++it) {
		    LogDebug(it->first << ": " << it->second);
			if(it->first == "X-File-Format") {
				file_format = types::file_format::file_format_t(rml::utils::strtoull(it->second.c_str(), 0ULL));
				LogDebug("Got file-format: "<<types::file_format::to_string(file_format));
			}
			else if(it->first == "ETag") {
				md5_etag = it->second.substr(1, 32);
				LogDebug("Got MD5: "<<md5_etag);
			}
		}

		if(file_format == types::file_format::unknown)
			throw exception(0, "Unknown file-type");

		fstream->re_open();
		{
			std::string md5;
			CryptoPP::Weak1::MD5 hash;
			CryptoPP::FileSource f(*fstream, true,
					new CryptoPP::HashFilter(hash,
							new CryptoPP::HexEncoder(
									new CryptoPP::StringSink(md5), false)));

			LogDebug("Computed MD5: "<<md5);

			if(md5 != md5_etag)
				throw exception(0, "File received corrupted");
		}

		if(stream_was_open)
			fstream->re_open();
		else
			fstream->close();

		const int status(response.status());

		LogDebug(boost::this_thread::get_id()<<" response: "<<status<<' '<<response.status_message());

		if(status != 200)
			throw exception(status, response.status_message());

		if(error)
			throw exception(0, "Unknown error");

		return file_format;
	}
};

}  // namespace app

#endif /* ARCHIVED_HPP_ */
