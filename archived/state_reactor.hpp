/*
 * state_reactor.hpp
 *
 *  Created on: 20/05/2012
 *      Author: dkorah
 */
#include "state_action.hpp"

#include <boost/threadpool.hpp>
#include <boost/threadpool/pool.hpp>
#include <boost/threadpool/scheduling_policies.hpp>
#include <boost/program_options.hpp>
#include <boost/function.hpp>

#ifndef STATE_REACTOR_HPP_
#define STATE_REACTOR_HPP_

namespace app {

template < typename PoolType = boost::threadpool::thread_pool< boost::function0< void >, boost::threadpool::fifo_scheduler > >
class state_reactor
{
	enum constants
	{
		min_threads = 5
	};

public:
	enum priority_t
	{
		very_low = 10,
		low = 20,
		medium = 30,
		high = 40,
		very_high = 50
	};

	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("state_reactor.thread_pool_size",  boost::program_options::value<unsigned short>()->default_value(min_threads), "StateMachine thread pool size")
				;
	}

	state_reactor(const boost::program_options::variables_map& confMap) :
		confMap_(confMap),
		tp_(std::max<unsigned short>(min_threads, confMap["state_reactor.thread_pool_size"].as<unsigned short>()))
	{
	}

	/**
	 * schedule an action on the state
	 *
	 * priority > 100 to be used for high priority stuff, like GET related states
	 *
	 * @param r
	 * @param priority
	 */
	bool schedule(const state_action& action, const priority_t& priority = medium, const bool& allow_exec_direct = true)
	{
		if (!allow_exec_direct || tp_.pending())
			return tp_.schedule(boost::threadpool::prio_task_func(priority, action));

		action();
		return true;
	}

	bool schedule(const state_action::fn_action& action, const state_action::fn_outcome_handler& outcome_handler, const priority_t& priority = medium, const bool& allow_exec_direct = true)
	{
		return schedule(state_action(action, outcome_handler), priority, allow_exec_direct);
	}

private:
	const boost::program_options::variables_map& confMap_;
	PoolType tp_;
};

}  // namespace app

#endif /* STATE_REACTOR_HPP_ */
