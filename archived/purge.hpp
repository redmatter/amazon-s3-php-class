/*
 * purge.hpp
 *
 *  Created on: 10 Oct 2011
 *      Author: dkorah
 */

#ifndef PURGE_HPP_
#define PURGE_HPP_

#include "operation_data.hpp"

namespace app {

class purge
{
	operation_data::channel_t& purge_channel_;
	database::internal& internal_db_;
	operation_data::busy_set_t& operation_data_busy_set_;
	const volatile bool& quit_;

	const boost::program_options::variables_map& confMap_;

	enum constants
	{
		min_interval_seconds = 30
	};

public:
	static void describe_config(boost::program_options::options_description& optsDesc)
	{
		optsDesc.add_options()
				("purge.interval_seconds",  boost::program_options::value<unsigned int>()->default_value(min_interval_seconds), "purge interval in seconds")
				;
	}

	purge(operation_data::channel_t& purge_channel, database::internal& internal_db, operation_data::busy_set_t& operation_data_busy_set,
			const volatile bool& quit, const boost::program_options::variables_map& confMap) :
		purge_channel_(purge_channel), internal_db_(internal_db), operation_data_busy_set_(operation_data_busy_set), quit_(quit), confMap_(confMap)
	{
	}

	void run()
	{
		int ticks = -20;
		const unsigned int& interval(std::max<unsigned int>(min_interval_seconds, confMap_["purge.interval_seconds"].as<unsigned int>()));

		std::set< operation_data::pointer > purge_set;

		while(!quit_) {
			try {
				if(ticks++ % interval) {
					try {
						operation_data::pointer opdata = purge_channel_.consume(1);

						for(files_map_t::const_iterator itr = opdata->p_detail->files_map.begin();
								itr != opdata->p_detail->files_map.end(); ++itr)
							itr->second->set_persistent(false);

						purge_set.insert(opdata);
						operation_data_busy_set_.remove(opdata); // RIP
					}
					catch(const operation_data::channel_t::consume_timed_out_exception& e) {
						// continue
					}

					continue;
				}
			}
			catch(const operation_data::channel_t::finishing_exception& e) {
				LogMessage(e.what());
				break;
			}
			catch(const std::exception& e) {
				LogWarning(e.what());
			}

			if(!purge_set.empty())
				if(internal_db_.purge(purge_set))
					purge_set.clear();
		}
	}
};

}  // namespace app


#endif /* PURGE_HPP_ */
