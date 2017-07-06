//
// Created by dkorah on 04/07/17.
//

#ifndef COLLECTD_ARCHIVABLE_ERROR_HPP
#define COLLECTD_ARCHIVABLE_ERROR_HPP

namespace collectd
{
namespace archivable
{
struct error
{
    enum enum_t
    {
        already_added = 0x01,
        not_found = 0x02,

        unknown = 0xff
    };
};

class archivable_error_category :
        public boost::system::error_category
{
public:
    const char *name() const
    {
        return "collectd.archivable";
    }

    std::string message(int ev) const
    {
        if (ev == error::already_added)
            return "Item already added";
        if (ev == error::not_found)
            return "Item not found";

        return "collectd.archivable unknown error";
    }
};

const boost::system::error_category &get_archivable_error_category()
{
    static archivable_error_category instance;
    return instance;
}

}
}

namespace boost
{
namespace system
{

template<>
struct is_error_code_enum<collectd::archivable::error::enum_t>
{
    static const bool value = true;
};

} // namespace system
} // namespace boost

namespace collectd
{
namespace archivable
{

inline boost::system::error_condition make_error_condition(const error::enum_t e)
{
    return boost::system::error_condition(static_cast<int>(e), get_archivable_error_category());
}

inline boost::system::error_condition &already_added_error()
{
    static boost::system::error_condition instance(make_error_condition(error::already_added));
    return instance;
}

inline boost::system::error_condition &not_found_error()
{
    static boost::system::error_condition instance(make_error_condition(error::not_found));
    return instance;
}

}
}

#endif //COLLECTD_ARCHIVABLE_ERROR_HPP
