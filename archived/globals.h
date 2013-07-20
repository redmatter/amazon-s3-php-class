/*
 * globals.h
 *
 *  Created on: 13 Jul 2010
 *      Author: dkorah
 */

#ifndef GLOBALS_H_
#define GLOBALS_H_

#include <rml/version.h>

#include <boost/program_options.hpp>

#include <string>

namespace app {

extern boost::program_options::variables_map confMap;

extern volatile bool quit;

extern volatile bool wasHUPed;

extern std::string hostname;

}  // namespace app

#endif /* GLOBALS_H_ */
