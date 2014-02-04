/*
 * test_boost_iostreams_bz2_decompress_hang.cpp
 *
 *  Created on: Dec 3, 2013
 *      Author: dkorah
 */
#include <rml/logger.h>
#include <gtest/gtest.h>

#include <sstream>
#include <vector>

#include "../../common/types.hpp"


int version_number[4];

TEST(Config, Read)
{
	std::vector<archiving::file_type::file_type_t> conf_types;
	std::string config("cdr,recording");

	if (!config.empty() && config != "all")
	{
		std::string tok;
		std::istringstream istr(config);
		while (std::getline(istr, tok, ','))
			if (!tok.empty())
				conf_types.push_back(archiving::file_type::from_string(tok));

		std::ostringstream ostr;
		const std::vector<archiving::file_type::file_type_t>::const_iterator& begin(conf_types.begin());
		const std::vector<archiving::file_type::file_type_t>::const_iterator& end(conf_types.end());
		for (std::vector<archiving::file_type::file_type_t>::const_iterator itr=begin; itr != end; ++itr)
			ostr << ((itr == begin)? ' ': ',') << (int)*itr;

		LogDebug(ostr.str());
	}
}
