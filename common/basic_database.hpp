/*
 * basic_database.hpp
 *
 *  Created on: 23 May 2011
 *      Author: dkorah
 */

#ifndef BASIC_DATABASE_HPP_
#define BASIC_DATABASE_HPP_

#include <rml/DBConnectionPool.h>
#include <rml/logger.h>

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/noncopyable.hpp>

#include <string>

namespace app {

namespace database {

template < typename database_type >
class basic_database : public boost::noncopyable
{
protected:
	const boost::program_options::variables_map& confMap_;
	rml::DBConnectionPool* p_pool_;

public:
	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		rml::DBConnectionPool::describe_config(optsDesc, database_type::get_identifier());
	}

	basic_database(const boost::program_options::variables_map& confMap) :
		confMap_(confMap), p_pool_(0)
	{
		const std::string& conf_username(boost::str( boost::format("mysql_%1%.username") % database_type::get_identifier() ));
		const std::string& username(confMap_[conf_username].as< std::string >());
		if(username.empty())
			throw rml::exception(std::string(database_type::get_identifier())+" database configuration missing");

		p_pool_ = new rml::DBConnectionPool(confMap_, database_type::get_identifier());

		LogVerbose(database_type::get_identifier()<<" database connection pool created");
	}

	virtual ~basic_database()
	{
		if(p_pool_)
			delete p_pool_;

		LogMessage("database['"<<database_type::get_identifier() << "] destroyed");
	}
};

} // namespace database
} // namespace app

#endif /* BASIC_DATABASE_HPP_ */
