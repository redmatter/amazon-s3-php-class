/*
 * state_action.hpp
 *
 *  Created on: 18/05/2012
 *      Author: dkorah
 */

#ifndef STATE_ACTION_HPP_
#define STATE_ACTION_HPP_

#include <boost/function.hpp>

namespace app {

class state_action
{
public:
	typedef boost::function< bool() > fn_action;
	typedef boost::function< void(bool) > fn_outcome_handler;

	state_action(fn_action action, fn_outcome_handler outcome_handler) : action_(action), outcome_handler_(outcome_handler)
	{
	}

	state_action(const state_action& copy) : action_(copy.action_), outcome_handler_(copy.outcome_handler_)
	{
	}

	void operator()()
	{
		bool result = false;
		try
		{
			result = action_();
		}
		catch(const std::exception& e) {
			LogWarning("Exception: "<<e.what());
		}
		catch(...) {
			LogWarning("Exception: Unexpected exception...");
		}

		try
		{
			outcome_handler_(result);
		}
		catch(const std::exception& e) {
			LogWarning("Exception [outcome_handler]: "<<e.what());
		}
		catch(...) {
			LogWarning("Exception [outcome_handler]: Unexpected exception...");
		}
	}

private:
	fn_action action_;
	fn_outcome_handler outcome_handler_;
};

}  // namespace app

#endif /* STATE_ACTION_HPP_ */
