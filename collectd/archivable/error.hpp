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
        already_added = 0x01
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

        return "collectd.archivable error";
    }
};

const boost::system::error_category& get_archivable_error_category()
{
    static archivable_error_category instance;
    return instance;
}

}
}

namespace boost {
namespace system {

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
inline boost::system::error_condition make_error_condition(const error::enum_t e) {
    return boost::system::error_condition(static_cast<int>(e), get_archivable_error_category());
}
}
}

#endif //COLLECTD_ARCHIVABLE_ERROR_HPP
