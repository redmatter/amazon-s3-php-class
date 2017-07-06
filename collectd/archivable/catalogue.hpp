//
// Created by dkorah on 27/06/17.
//

#ifndef COLLECTD_ARCHIVABLE_CATALOGUE_HPP
#define COLLECTD_ARCHIVABLE_CATALOGUE_HPP

#include "../../common/archivable.hpp"
#include "utils.hpp"
#include "error.hpp"

#include <rml/queue/detail/shared_ptr_less.hpp>

#include <boost/thread/mutex.hpp>
#include <boost/system/error_code.hpp>
#include <set>

namespace collectd
{
namespace archivable
{

struct catalogue :
        private boost::noncopyable,
        public boost::enable_shared_from_this<catalogue>
{
    typedef archiving::archivable::pointer item_t;
    typedef std::set<item_t, rml::queue::detail::shared_ptr_less<item_t> > set_t;

    void add(const item_t &item, boost::system::error_condition &ec)
    {
        ec.clear();

        boost::unique_lock<boost::mutex> lock(mutex_);

        const std::pair<set_t::iterator, bool> &ret(set_.insert(item));
        if (!ret.second) {
            ec = already_added_error();
        }
    }

    void remove(const item_t &item)
    {
        boost::unique_lock<boost::mutex> lock(mutex_);

        set_t::const_iterator itr = set_.find(item);
        if (itr != set_.end()) {
            set_.erase(itr);
        }
    }

    bool is_present(const item_t &item) const
    {
        boost::unique_lock<boost::mutex> lock(mutex_);
        set_t::const_iterator itr = set_.find(item);
        return itr != set_.end();
    }

    item_t get_item(const item_t &item, boost::system::error_condition &ec) const
    {
        boost::unique_lock<boost::mutex> lock(mutex_);
        set_t::const_iterator itr = set_.find(item);
        if (itr == set_.end()) {
            ec = not_found_error();
            return item_t();
        }

        return *itr;
    }

    std::size_t count() const
    {
        boost::unique_lock<boost::mutex> lock(mutex_);
        return set_.size();
    }

    struct item_sentry : private boost::noncopyable
    {
        item_sentry(const boost::shared_ptr<catalogue> &c, const item_t &i, const bool throw_on_error = true)
                : catalogue_(c), item_(i), owns_(false), cancelled_(false)
        {
            boost::system::error_condition ec;
            catalogue_->add(item_, ec);
            if (ec && throw_on_error) {
                throw boost::system::system_error(ec.value(), ec.category());
            }
            owns_ = true;
        }

        ~item_sentry()
        {
            if (owns_ && !cancelled_) {
                catalogue_->remove(item_);
            }
        }

        void cancel()
        {
            cancelled_ = true;
        }

    private:
        boost::shared_ptr<catalogue> catalogue_;
        const item_t item_;
        bool owns_;
        bool cancelled_;

        item_sentry &operator=(const item_sentry &other);
    };

private:
    set_t set_;
    mutable boost::mutex mutex_;
};

}
}

#endif //COLLECTD_ARCHIVABLE_CATALOGUE_HPP
