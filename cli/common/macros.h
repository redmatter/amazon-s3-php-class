/*
 * macros.h
 *
 *  Created on: 16 May 2016
 *      Author: dkorah
 */

#ifndef CLI_COMMON_MACROS_H_
#define CLI_COMMON_MACROS_H_


#define check_cli_option( confMap, option, message ) \
	do { \
		if (!confMap.count(option) || confMap[option].as<std::string>().empty()) { \
			throw rml::exception(message); \
		} \
	} while (false)


#endif /* CLI_COMMON_MACROS_H_ */
