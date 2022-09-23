//
// Created by dkorah on 20/09/22.
//

#ifndef ARCHIVING_MAX_SIZE_FILTER_HPP
#define ARCHIVING_MAX_SIZE_FILTER_HPP

#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/write.hpp>

#include <stdexcept>

namespace common {
namespace artefact {

struct max_size_filter : public boost::iostreams::multichar_output_filter
{
    typedef char char_type;
    typedef boost::iostreams::multichar_output_filter_tag category;

    const std::string name;
    const std::size_t max_size;

    std::size_t size;
    max_size_filter(const std::string& name, std::size_t max_size) : name(name), max_size(max_size), size(0) {}
    ~max_size_filter() { close(); }

    template<typename Sink>
    std::streamsize write(Sink& sink, const char_type* s, std::streamsize n)
    {
        std::streamsize written = boost::iostreams::write(sink, s, n);

        size += written;
        if (size >= max_size) {
            throw std::runtime_error(name + ": more than expected bytes");
        }

        return written;
    }

    template<typename Sink>
    void close(Sink& sink)
    {
        close();
    }

    void close() const { std::cout << name << ": bytes: " << size << std::endl; }
};

} // namespace artefact
} // namespace common

#endif // ARCHIVING_MAX_SIZE_FILTER_HPP
