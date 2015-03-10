#ifndef CLIENT_H
#define CLIENT_H

#include <string>

#include <sys/types.h>

#include <mime/object.h>

namespace mime
{

/**
 This interface describes all communication which describes the
 objects.  You get the MIME decoder to do something useful by
 implementing all these methods.
 */
class client_interface
{
public:

	virtual ~client_interface()
	{
	}

	/**
	 This method is called when a MIME object has been discovered in
	 the input data.  Data may or may not follow.
	 */
	virtual void object_created(object* object) = 0;

	/**
	 This method is caled when a MIME object's data starts processing.
	 */
	virtual void data_start(object* obj) = 0;

	/**
	 This method is used to deliver data parsed from the MIME
	 object.
	 */
	virtual void data(object* obj, unsigned char *data, int len) = 0;

	/**
	 This method is used to indicate that all data from the MIME
	 object has been delivered.
	 */
	virtual void data_end(object* obj) = 0;
};

}

#endif

