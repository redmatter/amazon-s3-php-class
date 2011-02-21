/*
 * exception.hpp
 *
 *  Created on: 17 Feb 2011
 *      Author: dkorah
 */

#ifndef EXCEPTION_HPP_
#define EXCEPTION_HPP_

#include <string>

#include <rml/exception.h>

namespace archiving {

namespace exception {

class retriable : public rml::exception {
	retriable(const std::string& err ) : rml::exception(err) { }
};

class nonretriable : public rml::exception {
	nonretriable(const std::string& err ) : rml::exception(err) { }
};

}  // namespace exception

}  // namespace archiving

#endif /* EXCEPTION_HPP_ */
