#ifndef DECODE_H
#define DECODE_H

#include <iostream>
#include <map>
#include <string>

#include <sys/types.h>

#include <mime/field.h>
#include <mime/object.h>
#include <mime/parser.h>
#include <mime/client.h>
#include <mime/exception.h>
#include <mime/parser_implementations.h>

/**
 \mainpage mimedecode
 
 \section intro Introduction
 
 This library currently consists of a single feature, capable of decoding
 MIME data.

 \section mimedecode Decoding MIME

 To use this library to decode MIME data, you need to study the decoder
 class, and the client_interface class.  To do something useful with this
 library, you need to provide a class, which is derived from the decoder
 class.  In deriving the class, you need to implement all the methods in the
 client_interface interface.

 Also, see simple.C on the example page.

 */

/**
 \example simple.C
 */

/**
 Namespace for all MIME handling things.
 */
namespace mime
{

/**
 A class which knows how to decode MIME.  You get this class to do
 something useful by deriving your own class from it, and implementing
 all the calls in the client_interface interface.
 */
class decoder: public client_interface
{
private:
	parser* p;
	object* obj;
public:
	/**
	 Default constructor.
	 */
	decoder()
	{
		p = 0;
		obj = new object();
	}
	/**
	 The data producer should call this once all data has been presented
	 to the decoder to finalise processing and clean up all resources.
	 */
	void close()
	{
		if (p)
		{
			p->close();
			delete p;
		}
		delete obj;
	}
	/**
	 The data produced should call this method to present data to the
	 decoder.
	 */
	void decode(unsigned char c);

	virtual ~decoder()
	{
	}
};

}

#endif

