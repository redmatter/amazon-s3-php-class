/*
 * coreapi.hpp
 *
 *  Created on: 15 Feb 2011
 *      Author: dkorah
 */

#ifndef COREAPI_HPP_
#define COREAPI_HPP_

#include <boost/network/protocol/http/client.hpp>

#include <rml/logger.h>

namespace app {

class coreapi {
	const boost::program_options::variables_map& confMap_;
	typedef boost::network::http::basic_client< boost::network::http::tags::http_async_8bit_udp_resolve, 1,1 > http_client;

	http_client client_;
	std::string authorization_;

public:
	static void describe_config(boost::program_options::options_description& optsDesc) {
		optsDesc.add_options()
				("coreapi.host", boost::program_options::value<std::string>()->default_value("api.redmatter.com"), "CoreAPI hostname")
				("coreapi.auth_hash", boost::program_options::value<std::string>()->default_value(""), "CoreAPI hash for a system user")
//				("coreapi.pool_size",  boost::program_options::value<unsigned int>()->default_value(5), "CoreAPI connection pool size")
				;
	}

	coreapi(const coreapi& copy) :
		confMap_(copy.confMap_), client_(http_client::follow_redirects), authorization_(copy.authorization_)
	{
	}

	coreapi(const boost::program_options::variables_map& confMap) :
		confMap_(confMap), client_(http_client::follow_redirects), authorization_()
	{
		get_authorized();
	}

	void get_authorized() {
		authorization_.clear();

		http::client::request authorization(std::string("http://")+confMap_["coreapi.host"].as<std::string>()+"/auth/"+confMap_["coreapi.auth_hash"].as<std::string>());
		http_client::response response = client_.get(authorization);

		LogVerbose("response: "<<boost::network::body(response));

		// run xpath
		// set authorization
	}

	std::string get(const std::string& uri) {
		http::client::request request(std::string("http://")+confMap_["coreapi.host"].as<std::string>()+uri);
		request << boost::network::header("Authorization", authorization_);
		http_client::response response = client_.get(request);

		LogVerbose("response: "<<boost::network::body(response));

		return boost::network::body(response);
	}
};

}  // namespace app

#endif /* COREAPI_HPP_ */
