/*
 * cache.hpp
 *
 *  Created on: 02 Feb 2011
 *      Author: dkorah
 */

#ifndef CACHE_HPP_
#define CACHE_HPP_

#include <rml/utils.h>
#include <rml/logger.h>

#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>
#include <boost/type_traits/remove_reference.hpp>
#include <boost/function.hpp>

#include <map>
#include <algorithm>

#include <time.h>

namespace app {

namespace cache {

template <
	typename Arg,
	typename Result,
	typename Arg_Compare = std::less< typename boost::remove_reference<Arg>::type >
>
class result_cache
{
		typedef typename boost::remove_reference<Arg>::type key_type;
		typedef Result val_type;

		typedef boost::function< Result (Arg) > function_type;

//		typedef Result (*fun_type) (Arg);

		struct timed_val_type
		{
			time_t atime;
			val_type value;

			timed_val_type() : atime(0), value()
			{
			}

			timed_val_type& operator=(const val_type& rhs)
			{
				value = rhs;
				time(&atime);
			}
		};

		typedef std::map< key_type, timed_val_type, Arg_Compare > cache_type;
		typedef typename cache_type::iterator iter_type;

		const function_type fun_;
		mutable cache_type cache_;

		mutable boost::mutex mutex_;

	public:
		result_cache (const function_type& fun) : fun_(fun)
		{
			LogDebug("fun_: empty? "<<fun_.empty());
		}

		~result_cache()
		{
			LogDebug("Destroyed");
		}

		Result operator() (Arg arg) const
		{
			boost::unique_lock<boost::mutex> lock(mutex_);

			iter_type it = cache_.find(arg);
			if (it != cache_.end()) {
				time(&it->second.atime);
				return it->second.value;
			}

			timed_val_type t_value;
			{
				t_value.value = fun_(arg);
				time(&t_value.atime);
				cache_[arg] = t_value;
			}

			return t_value.value;
		}

		void clear_unused(time_t diff)
		{
			boost::unique_lock<boost::mutex> lock(mutex_);

			LogVerbose("Removing older cache items");

			time_t newer = time(0) - diff;
			iter_type cache_end = cache_.end();
			for(iter_type it = cache_.begin(); it != cache_end; ++it) {
				if(it->second.atime < newer)
					cache_.erase(it);
			}
		}
};

//template <typename Arg, typename Result>
//result_cache<Arg, Result> make_result_cache (Result (*f) (Arg)) {
//	return result_cache<Arg, Result>(f);
//}

}  // namespace cache

}  // namespace app


#endif /* CACHE_HPP_ */
