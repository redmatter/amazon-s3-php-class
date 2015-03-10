#include <mime/decode.h>

#include <iostream>

#include <stdexcept>

using namespace std;

class my_decoder: public mime::decoder
{
public:
	my_decoder()
	{
	}
	virtual void object_created(mime::object* object);
	virtual void data_start(mime::object* obj);
	virtual void data(mime::object* obj, unsigned char *data, int len);
	virtual void data_end(mime::object* obj);
};

void my_decoder::object_created(mime::object* object)
{

	cerr << "*** New object type " << object->type << "/" << object->subtype
			<< endl;

	map<string, mime::field>::iterator i;
	for (i = object->fields.begin(); i != object->fields.end(); i++)
	{
		cerr << "  " << i->first << ":" << i->second.value << endl;
		map<string, string>::iterator j;
		for (j = i->second.attributes.begin(); j != i->second.attributes.end();
				j++)
		{
			cerr << "    " << j->first << ":" << j->second << endl;
		}
	}

}

void my_decoder::data_start(mime::object* object)
{
	cerr << "*** Data start " << object->type << "/" << object->subtype << endl;
}

void my_decoder::data(mime::object* object, unsigned char *data, int len)
{
//    cerr << "*** Data (" << len << ")" << endl;
	for (int i = 0; i < len; i++)
		cerr << data[i];
}

void my_decoder::data_end(mime::object* object)
{
	cerr << "*** Data end " << object->type << "/" << object->subtype << endl;
}

int main(int argc, char **argv)
{

	try
	{

		my_decoder d;

		while (cin.good())
		{
			unsigned char c = cin.get();
			if (cin.good())
				d.decode(c);
		}

		d.close();

	} catch (exception& e)
	{
		cerr << "Exception: " << e.what() << endl;
	}

}

