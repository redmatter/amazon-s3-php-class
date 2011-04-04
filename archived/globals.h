/*
 * globals.h
 *
 *  Created on: 13 Jul 2010
 *      Author: dkorah
 */

#ifndef GLOBALS_H_
#define GLOBALS_H_

#include <boost/program_options.hpp>

#include <string>

#ifdef __cplusplus
extern "C" {
#endif

// from $(TMP)/version.c
extern int version_number[3];
extern const char *version_number_string;
extern const char *version_number_only_string;
extern const char *version_date;
extern const char *build_host;
extern const char *copyright_string;

#ifdef __cplusplus
}
#endif

namespace app {

extern boost::program_options::variables_map confMap;

extern volatile bool quit;

extern volatile bool wasHUPed;

}  // namespace app

#endif /* GLOBALS_H_ */
