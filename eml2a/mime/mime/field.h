#ifndef FIELD_H
#define FIELD_H

#include <string>
#include <map>

#include <sys/types.h>

namespace mime
{

/**
 Describes a MIME object header field.
 */
class field
{
public:
	/** 
	 The field key e.g. "content-type". Note that the key
	 is converted to lower case.
	 */
	std::string key;

	/**
	 The field value.
	 */
	std::string value;

	/**
	 The attributes associated with the field.  This is a map
	 from key to value.
	 */
	std::map<std::string, std::string> attributes;
};

}

#endif

