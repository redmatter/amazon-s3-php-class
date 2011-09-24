/*
 * globals.h
 *
 *  Created on: 13 Jul 2010
 *      Author: dkorah
 */

#ifndef GLOBALS_H_
#define GLOBALS_H_

#include <string>

#include <rml/version.h>

namespace app {

extern std::string hostname;

extern volatile bool quit;
extern volatile bool wasHUPed;

}  // namespace app

#endif /* GLOBALS_H_ */
