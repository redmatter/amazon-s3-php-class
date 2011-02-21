/*
 * cache.hpp
 *
 *  Created on: 02 Feb 2011
 *      Author: dkorah
 */

#ifndef CACHE_HPP_
#define CACHE_HPP_

#include <time.h>

#include <boost/thread/mutex.hpp>
#include <boost/type_traits/remove_reference.hpp>

namespace app {

namespace cache {

template <typename Arg, typename Result>
class result_cache {
		typedef boost::remove_reference<Arg>::type key_type;
		typedef Result val_type;
		typedef Result (*fun_type) (Arg);

		struct timed_val_type {
			time_t atime;
			val_type value;

			timed_val_type() : atime(0), value()
			{
			}

			timed_val_type& operator=(const val_type& rhs) {
				value = rhs;
				atime = time(0);
			}
		};

		typedef std::map<key_type, timed_val_type> cache_type;
		typedef typename cache_type::iterator iter_type;

		const fun_type fun_;
		mutable cache_type cache_;

		mutable boost::mutex mutex_;

	public:
		result_cache (fun_type fun) :fun_(fun) {}

		Result operator() (Arg arg) const {
			boost::unique_lock<boost::mutex> lock(mutex_);

			iter_type it = cache_.find(arg);
			if (it != cache_.end()) {
				time(&it->second.atime);
				return it->second.value;
			}

			cache_[arg] = fun_(arg);
		}

		void clear_unused(time_t diff) {
			boost::unique_lock<boost::mutex> lock(mutex_);

			time_t newer = time(0) - diff;
			iter_type cache_end = cache_.end();
			for(iter_type it = cache_.begin(); it != cache_end; ++it) {
				if(it->second.atime < newer)
					cache_.erase(it);
			}
		}
};

template <typename Arg, typename Result>
result_cache<Arg, Result> make_result_cache (Result (*f) (Arg)) {
	return result_cache<Arg, Result>(f);
}

}  // namespace cache

}  // namespace app


#endif /* CACHE_HPP_ */
