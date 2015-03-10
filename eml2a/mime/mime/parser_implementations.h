#ifndef PARSER_IMPLEMENTATIONS_H
#define PARSER_IMPLEMENTATIONS_H

#include <string>

#include <mime/field.h>
#include <mime/object.h>
#include <mime/client.h>
#include <mime/parser.h>

#include <stdint.h>

namespace mime
{

class base64_parser: public object_body_parser
{
	std::string encoding;
	uint32_t buffer;
	int buflen;
	int pad;
public:
	base64_parser(client_interface* client, object *obj) :
			object_body_parser(client, obj)
	{

		client->data_start(obj);

		buflen = 0;
		buffer = 0;
		pad = 0;
		encoding = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
				"abcdefghijklmnopqrstuvwxyz"
				"0123456789+/";
	}
	virtual void parse(unsigned char c);
	void close()
	{
		client->data_end(obj);
	}
};

/**
 \todo The quoted printable parser doesn't work.
 */
class quotedprintable_parser: public object_body_parser
{
private:
	uint32_t buffer;
	int buflen;
	enum
	{
		NORMAL, SPECIAL, SOFTBREAK
	} state;
public:
	quotedprintable_parser(client_interface* client, object *obj) :
			object_body_parser(client, obj)
	{

		client->data_start(obj);

		buflen = 0;
		buffer = 0;
		state = NORMAL;
	}
	virtual void parse(unsigned char c);
	void close()
	{
		client->data_end(obj);
	}
};

class multipart_parser: public object_body_parser
{
private:
	parser* sub_parser;
	object* sub_obj;

	// States:
	// PRE - Searching for first boundary
	// ATBOUND - Just matched the whole boundary std::string.
	// FOLLBOUND - Looking at characters after boundary
	// INSIDE - Processing stuff between boundaries
	enum
	{
		PRE, ATBOUND, FOLLBOUND, INSIDE, DONE, STARTOBJECT
	} state;
	std::string boundary;
	std::string buffer;
	int posn;
public:
	multipart_parser(client_interface* client, object* obj);
	virtual void parse(unsigned char c);
	void close();
};

class simple_parser: public object_body_parser
{
public:
	simple_parser(client_interface* client, object *obj) :
			object_body_parser(client, obj)
	{
		client->data_start(obj);
	}
	virtual void parse(unsigned char c);
	void close()
	{
		client->data_end(obj);
	}
};

/**
 \todo Could this have less stuff?  All these strings, *and* a field
 structure?
 */
class header_parser: public parser
{
private:
	object *obj;

	std::string key;
	std::string value;
	std::string attrkey;
	std::string attrval;

	// Fairly complex set of states for header parsing:
	// PREKEY - Looking out for key.
	// KEY - Gathering key.
	// PREVAL - Looking out for a value.
	// VAL - reading value.
	// QTVAL - reading a quoted value.
	// PREATTRKEY - looking out for an attribute key
	// ATTRKEY - reading an attribute key
	// ATTRVAL - reading an attribute val
	// QTATTRVAL - reading a quoted attribute value
	// EOL - read an end-of-line
	enum
	{
		KEY, VAL, ATTRKEY, ATTRVAL, PREKEY, PREVAL, POSTVAL, EOL, QTVAL, CR
	} state;
	std::map<std::string, std::string> attributes;
	void add_header(const std::string& key, const std::string& value);
	void parse_attrs(field& fld);
	void split(std::string& x, std::string& y, std::string& z,
			unsigned char spl);
public:
	header_parser(object* obj)
	{
		this->obj = obj;
		state = PREKEY;
	}
	virtual void parse(unsigned char c);
};

class any_object_parser: public object_parser
{
private:
	parser* p;
	enum
	{
		HEADER, HEADER_COMPLETE, BODY
	} state;
public:
	any_object_parser(client_interface* client, object* obj) :
			object_parser(client, obj)
	{
		p = new header_parser(obj);
		state = HEADER;
	}
	virtual void parse(unsigned char c);
	void close()
	{
		if (p)
		{
			p->close();
			delete p;
		}
	}
};

class parser_factory
{
public:
	static parser* generic(client_interface* client, object* obj);
	static parser* create(client_interface* client, object* obj);
};

}

#endif

