#ifndef PARSER_H
#define PARSER_H

#include <string>

#include <mime/client.h>
#include <mime/object.h>

namespace mime
{

class parser
{
public:
	virtual void parse(unsigned char c) = 0;
	virtual void close()
	{
	}
	virtual ~parser()
	{
	}
};

class object_parser: public parser
{
protected:
	client_interface* client;
	object *obj;
public:
	object_parser(client_interface* client, object* obj)
	{
		this->client = client;
		this->obj = obj;
		client->data_start(obj);
	}
	virtual ~object_parser()
	{
		client->data_end(obj);
	}
};

class object_body_parser: public object_parser
{
public:
	object_body_parser(client_interface* client, object* obj) :
			object_parser(client, obj)
	{
	}
};

}

#endif

