/*
 * cdr_inspector.hpp
 *
 *  Created on: 25 Jan 2011
 *      Author: dkorah
 */

#ifndef CDR_INSPECTOR_HPP_
#define CDR_INSPECTOR_HPP_

#include <boost/noncopyable.hpp>

#include "../common/types.hpp"

#include "globals.h"

namespace app {

class cdr_inspector : public boost::noncopyable {

	archiving::archived_file_channel_t& channel;

public:
	cdr_inspector(archiving::archived_file_channel_t& channel) : channel(channel) {}

	void run() {
		while(!app::quit) {
			try {
				archiving::archived_file::pointer file = channel.consume();
				LogVerbose(file->get_file_path()<<": md5: "<<file->get_md5());
			}
			catch(const archiving::archived_file_channel_t::finishing_exception& e) {
				LogVerbose("cdr_inspector::run - "<<e.what());
				break;
			}
			catch(const std::exception& e) {
				LogError("cdr_inspector::run - exception:"<<e.what());
			}
			catch(...) {
				LogCritical("cdr_inspector::run - unexpected exception");
			}
		}
		LogMessage("cdr_inspector::run - finishing");
	}
};

}  // namespace app

#endif /* CDR_INSPECTOR_HPP_ */
