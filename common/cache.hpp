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
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int.hpp>
#include <boost/random/variate_generator.hpp>

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

//	typedef Result (*fun_type) (Arg);

	struct timed_val_type
	{
		mutable time_t atime;
		const time_t eol_time;
		const val_type value;

		timed_val_type(val_type value, boost::variate_generator<boost::mt19937&, boost::uniform_int<> >& rander) :
			atime(time(0)), eol_time(atime + rander()), value(value)
		{
			LogDebug(value<<" end-of-life:"<<eol_time<<" access-time:"<<atime);
		}
	};

	typedef std::map< key_type, timed_val_type, Arg_Compare > cache_type;
	typedef typename cache_type::iterator iter_type;

	const function_type fun_;
	mutable cache_type cache_;

	mutable boost::mutex mutex_;

	boost::mt19937 rand_gen_;
	boost::uniform_int<> rand_dist_;
	mutable boost::variate_generator<boost::mt19937&, boost::uniform_int<> > rander_;

	const unsigned int max_idle_seconds_;

	const std::string name_;

	public:
	result_cache (const function_type& fun, const std::string& name, const int& min_max_age_minutes = 5, const int& max_max_age_minutes = 10, const unsigned int& max_idle_seconds = 30) :
		fun_(fun), rand_gen_(), rand_dist_(min_max_age_minutes*60, max_max_age_minutes*60), rander_(rand_gen_, rand_dist_), max_idle_seconds_(max_idle_seconds), name_(name+": ")
	{
	}

	~result_cache()
	{
	}

	Result operator() (Arg arg) const
	{
		boost::unique_lock<boost::mutex> lock(mutex_);

		iter_type it = cache_.find(arg);
		if (it != cache_.end()) {
			time(&it->second.atime);
			LogDebug(name_<<"Found from cache; "<<it->second.value<<" end-of-life:"<<it->second.eol_time<<" access-time:"<<it->second.atime);
			return it->second.value;
		}

		timed_val_type t_value(fun_(arg), rander_);
		cache_.insert(std::make_pair(arg, t_value));
		LogVerbose(name_<<"Memorising "<<t_value.value);
		return t_value.value;
	}

	void forget(Arg arg, const unsigned int in_seconds = 0)
	{
		boost::unique_lock<boost::mutex> lock(mutex_);

		iter_type it = cache_.find(arg);
		if (it != cache_.end()) {
			LogVerbose(name_<<"Forgetting (immediately) "<<it->second.value);
			cache_.erase(it);
		}
	}

	void clear_unused(const unsigned int& max_idle_seconds = 0)
	{
		boost::unique_lock<boost::mutex> lock(mutex_);

		const unsigned int& max_idle(max_idle_seconds? max_idle_seconds: max_idle_seconds_);

		LogVerbose(name_<<"Removing older cache items");

		const time_t now( time(0) );
		const time_t newer( now - max_idle );

		iter_type cache_end = cache_.end();
		for(iter_type it = cache_.begin(); it != cache_end; ) {
			if((it->second.eol_time < now) || (it->second.atime < newer)) {
				LogDebug(name_<<"Deleting from cache; "<<it->second.value<<" end-of-life:"<<it->second.eol_time<<" access-time:"<<it->second.atime);
				LogVerbose(name_<<"Forgetting "<<it->second.value);
				cache_.erase(it++);
			}
			else
				++it;
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
