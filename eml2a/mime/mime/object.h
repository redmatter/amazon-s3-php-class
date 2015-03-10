#ifndef OBJECT_H
#define OBJECT_H

#include <string>
#include <list>

#include <sys/types.h>

#include <mime/field.h>

namespace mime
{

/**
 This object describes a MIME object.
 */
class object
{
public:
	/**
	 The default constructor.
	 */
	object()
	{
		parent = 0;
	}
	/**
	 The object's major type, derived from the MIME type
	 e.g. "text"
	 */
	std::string type;

	/**
	 The object's minor type, derived from the MIME type
	 e.g. "plain"
	 */
	std::string subtype;

	/**
	 All fields from the object's header
	 */
	std::map<std::string, field> fields;

	/**
	 Pointer to the parent object, if one exists.
	 */
	object* parent;
};

}

#endif

