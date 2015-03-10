
#include <mime/decode.h>
#include <mime/exception.h>
#include <mime/field.h>
#include <mime/object.h>
#include <mime/parser.h>

#include <stdexcept>
#include <sstream>
#include <cctype>

using namespace mime;

void decoder::decode(unsigned char c)
{

    if (!p)
	p = parser_factory::generic(this, obj);

    p->parse(c);

}
