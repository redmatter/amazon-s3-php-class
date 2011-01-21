/*
 * esl.h
 *
 *  Created on: 17 Jan 2011
 *      Author: dkorah
 */

#ifndef ESL_H_
#define ESL_H_

#include <boost/shared_ptr.hpp>

#include <vector>
#include <string>

namespace esl {

#include <esl/esl_oop.h>

typedef boost::shared_ptr<ESLconnection> ESLconnectionPtr;
typedef boost::shared_ptr<ESLevent> ESLeventPtr;

/// the filter type
typedef std::vector< std::pair<std::string, std::string> > filters;

}
#endif /* ESL_H_ */
