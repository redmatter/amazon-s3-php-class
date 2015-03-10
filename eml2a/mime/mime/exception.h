#ifndef EXCEPTION_H
#define EXCEPTION_H

#include <exception>
#include <stdexcept>

namespace mime
{

class error: public std::logic_error
{
public:
	error(const std::string& msg) :
			std::logic_error(msg)
	{
	}
};

class end_of_header: public std::logic_error
{
public:
	end_of_header(const std::string& msg) :
			std::logic_error(msg)
	{
	}
};

}

#endif /* _GLIBCXX_STDEXCEPT */
