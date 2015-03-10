#include <mime/parser.h>
#include <mime/parser_implementations.h>
#include <mime/exception.h>

using namespace mime;

void base64_parser::parse(unsigned char c)
{
	if (c == '=')
	{
		buffer = buffer << 6;
		pad += 6;
		buflen++;
	}
	else
	{
		size_t pos = encoding.find(c);
		if (pos != std::string::npos)
		{
			buffer = buffer << 6;
			buffer = buffer | ((unsigned int) pos & 0x3f);
			buflen++;
		}
	}

	if (buflen == 4)
	{
		for (int i = 0; i < 3; i++)
		{
			if ((i * 8) > (16 - pad))
				continue;
			unsigned char c = (buffer & 0xff0000) >> 16;
			client->data(obj, &c, 1);
			buffer = buffer << 8;
		}
		buflen = 0;
		buffer = 0;
		pad = 0;
	}
}

void any_object_parser::parse(unsigned char c)
{
	if (state == HEADER_COMPLETE)
	{
		delete p;
		state = BODY;

		client->object_created(obj);

		// Use the parser factory to get an appropriate parser.
		p = parser_factory::create(client, obj);
	}

	try
	{
		p->parse(c);
	}
	catch (end_of_header& e)
	{
		state = HEADER_COMPLETE;
	}
}

void simple_parser::parse(unsigned char c)
{
	client->data(obj, &c, 1);
}

void quotedprintable_parser::parse(unsigned char c)
{
	if (state == SPECIAL)
	{
		if (c == '\r' || c == '\n')
			state = SOFTBREAK;
		else
		{
			static const std::string v = "0123456789ABCDEF";
			size_t pos = v.find(c);
			if (pos == std::string::npos)
				return;
			buffer = buffer << 4 | (((unsigned int) v.find(c)) & 0xF);
			buflen++;
			if (buflen == 2)
			{
				c = (unsigned char) buffer;
				client->data(obj, &c, 1);
				state = NORMAL;
			}
		}
		return;
	}
	else if (state == SOFTBREAK)
	{
		if (c == '\r' || c == '\n')
			return;
		state = NORMAL;
	}

	if (c == '=')
	{
		state = SPECIAL;
		buflen = 0;
		return;
	}

	// Normal character, output it
	client->data(obj, &c, 1);
}

parser* parser_factory::generic(client_interface* client, object* obj)
{
	return new any_object_parser(client, obj);
}

parser* parser_factory::create(client_interface* client, object* obj)
{
	std::string encoding = obj->fields["content-transfer-encoding"].value;
	if (obj->type == "multipart")
		return new multipart_parser(client, obj);
	else if (encoding == "base64")
		return new base64_parser(client, obj);
	else if (encoding == "quoted-printable")
		return new quotedprintable_parser(client, obj);
	else
		return new simple_parser(client, obj);
}
