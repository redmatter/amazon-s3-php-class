/*
 * compress.hpp
 *
 *  Created on: 17 Feb 2011
 *      Author: dkorah
 */

#ifndef COMPRESS_HPP_
#define COMPRESS_HPP_

#include <string.h>

#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/write.hpp>
#include <boost/iostreams/filter/bzip2.hpp>

#include <rml/exception.h>
#include <rml/logger.h>

namespace app {

namespace bzip2 {

/**
 * Compress content_length bytes from is into os
 *
 * @param is
 * @param os
 * @param content_length
 * @return number of bytes inserted into os
 */
size_t compress(std::istream& is, std::ostream& os, const size_t content_length = 0) {
	try {
		std::streampos start = os.tellp();

		boost::iostreams::filtering_streambuf<boost::iostreams::output> filters;
		filters.set_auto_close(false);
		filters.push(boost::iostreams::bzip2_compressor());
		filters.push(os);

		if(content_length > 0) {
			size_t copied_length = 0;
			while(copied_length < content_length) {
				size_t buf_length = std::min<size_t>(content_length - copied_length, 8196);
				std::string buf(buf_length);

				buf_length = is.read(&buf[0], buf_length);
				boost::iostreams::write(filters, &buf[0], buf_length);

				copied_length += buf_length;

				LogVerbose("Copied: "<<buf_length<<" ("<<copied_length<<'/'<<content_length<<')'<<" bytes");
			}
		}
		else {
			std::streamsize copied = boost::iostreams::copy(is, filters);
			LogVerbose("Copied: "<<copied<<" bytes");
		}

		return ( size_t ) ( os.tellp() - start );
	}
	catch(const std::exception& e) {
		LogError("Exception: "<<e.what());
		throw;
	}
	catch(...) {
		LogError("Unknown exception");
		throw;
	}
}

/**
 * Decompress content_length bytes from is into os
 *
 * @param is
 * @param os
 * @param content_length
 * @return number of bytes inserted into os
 */
size_t decompress(std::istream& is, std::ostream& os, const size_t content_length = 0) {
	try {
		std::streampos start = os.tellp();

		boost::iostreams::filtering_streambuf<boost::iostreams::output> filters;
		filters.set_auto_close(false);
		filters.push(boost::iostreams::bzip2_decompressor());
		filters.push(os);

		if(content_length > 0) {
			size_t copied_length = 0;
			while(copied_length < content_length) {
				size_t buf_length = std::min<size_t>(content_length - copied_length, 8196);
				std::string buf(buf_length);

				buf_length = is.read(&buf[0], buf_length);
				boost::iostreams::write(filters, &buf[0], buf_length);

				copied_length += buf_length;

				LogVerbose("Copied: "<<buf_length<<" ("<<copied_length<<'/'<<content_length<<')'<<" bytes");
			}
		}
		else {
			std::streamsize copied = boost::iostreams::copy(is, filters);
			LogVerbose("Copied: "<<copied<<" bytes");
		}

		return ( size_t ) ( os.tellp() - start );
	}
	catch(const boost::iostreams::bzip2_error& e) {
		int error = e.error();

		if(error == boost::iostreams::bzip2::data_error)
			throw rml::exception("corrupted compressed data");
		else if(error == boost::iostreams::bzip2::data_error_magic)
			throw rml::exception("the 'magic' sequence 'BZh' missing");
		else if(boost::iostreams::bzip2::config_error)
			throw rml::exception("libbzip2 config error (not what it says most of the time)");
		else
			THROW_RML_EXCEPTION("Unexpected bzip2 error - err: " << e.what() << " (" << error << ')');
	}
	catch(const std::exception& e) {
		LogError("Exception: "<<e.what());
		throw;
	}
	catch(...) {
		LogError("Unknown exception");
		throw;
	}
}

}  // namespace bzip2

}  // namespace app

#endif /* COMPRESS_HPP_ */
