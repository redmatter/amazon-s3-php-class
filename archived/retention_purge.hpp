/*
 * retention_purge.hpp
 *
 *  Created on: 9 Feb 2012
 *      Author: dkorah
 */

#ifndef RETENTION_PURGE_HPP_
#define RETENTION_PURGE_HPP_

namespace app {

/**
 * retention policy based purge
 * Here we identify the purge set and feed them into storage operations channel
 */
class retention_purge
{
	operation_data::channel_t& purge_channel_;
	database::retention_ro& retention_ro_db_;
	database::retention& retention_db_;

	volatile bool& quit_;

	retention_purge(operation_data::channel_t& purge_channel,
			database::retention_ro& retention_ro_db, database::retention& retention_db,
			volatile bool& quit) :
				purge_channel_(purge_channel),
				retention_ro_db_(retention_ro_db),
				retention_db_(retention_db),
				quit_(quit)

	{
	}

	/**
	 * run thread
	 */
	void run()
	{
		int iter = 1;

		while(!quit_) {
			if(iter % 30) {
				boost::this_thread::sleep(boost::posix_time::seconds(1));
				continue;
			}

			try {

			}
			catch(const std::exception& e) {
				LogWarning(e.what());
			}
		}
	}
};

}  // namespace app

#endif /* RETENTION_PURGE_HPP_ */
