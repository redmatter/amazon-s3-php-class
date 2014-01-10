/*
 * compress.hpp
 *
 *  Created on: 17 Feb 2011
 *      Author: dkorah
 */

#ifndef COMPRESS_HPP_
#define COMPRESS_HPP_

#include "operation_data.hpp"

#include <rml/exception.h>
#include <rml/logger.h>

#include <cryptopp/crc.h>
#include <cryptopp/files.h>
#include <cryptopp/filters.h>

#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/write.hpp>
#include <boost/iostreams/filter/bzip2.hpp>

#include <algorithm>
#include <iterator>
#include <istream>

#include <string.h>

namespace internal
{

struct safe_ostream_sink
{
	typedef char char_type;
	typedef boost::iostreams::sink_tag category;

	std::ostream& os;
	bool& failure;

	safe_ostream_sink(std::ostream& os, bool& failure) :
			os(os), failure(failure)
	{
		failure = false;
	}

	safe_ostream_sink(const safe_ostream_sink& other) :
			os(other.os), failure(other.failure)
	{
	}

	std::streamsize write(const char* s, std::streamsize n)
	{
		if (failure)
			return n;

		try
		{
			os.write(s, n);
		}
		catch(...) {
			failure = true;
			throw;
		}

		if (!os)
			failure = true;

		if (failure)
			throw std::ios::failure("Writing to stream failed");

		return n;
	}

private:
	safe_ostream_sink& operator=(const safe_ostream_sink& other);
};

struct safe_istream_source
{
	typedef char char_type;
	typedef boost::iostreams::source_tag category;

	std::istream& is;
	bool& failure;

	safe_istream_source(std::istream& os, bool& failure) :
			is(os), failure(failure)
	{
		failure = false;
	}

	safe_istream_source(const safe_istream_source& other) :
			is(other.is), failure(other.failure)
	{
	}

	std::streamsize read(char* s, std::streamsize n)
	{
		if (failure)
			return -1;

		std::streamsize ret(0);
		try
		{
			ret = is.readsome(s, n);
			DevDebug(" ..................... ret: "<<ret);
			if (ret == 0)
				return -1;
		}
		catch(...) {
			failure = true;
			throw;
		}

		if (!is)
			failure = true;

		if (failure)
			throw std::ios::failure("Reading from stream failed");

		return n;
	}

private:
	safe_istream_source& operator=(const safe_istream_source& other);
};

}  // namespace internal

namespace app {

class bzip2
{
public:
	/**
	 * Compress content_length bytes from is into os
	 *
	 * @param is
	 * @param os
	 * @param content_length
	 * @return number of bytes inserted into os
	 */
	static size_t compress(std::istream& is, std::ostream& os, const size_t content_length = 0)
	{
		try {

			std::streampos start = os.tellp();

			{	// important to be in a separate scope!
				// filters wouldn't flush and close ... could call one of its methods ... but ... who could be bothered

				bool ostr_failure(false);
				bool istr_failure(false);

				boost::iostreams::filtering_streambuf<boost::iostreams::output> filters;
				filters.push(boost::iostreams::bzip2_compressor());
				filters.push(internal::safe_ostream_sink(os, ostr_failure));

				if(content_length > 0) {
					size_t copied_length = 0;
					while(!ostr_failure && (copied_length < content_length)) {
						size_t buf_length = std::min<size_t>(content_length - copied_length, 8196);

						std::vector<char> buf(buf_length);

						is.read(&buf[0], buf_length);
						buf_length = is.gcount();

						if(!buf_length)
							throw std::ios::failure("compress : Reading from input stream failed");

						boost::iostreams::write(filters, &buf[0], buf_length);

						copied_length += buf_length;
					}
				}
				else {
					/*std::streamsize copied = */boost::iostreams::copy(
							internal::safe_istream_source(is, istr_failure), filters);
				}

				if (ostr_failure || istr_failure)
					throw std::runtime_error("Failed to compress (stream error)");
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

	static void compress(app::operation_data::pointer opdata, const types::file_format::file_format_t& compressed_format)
	{
		if(types::file_format::is_compressed(opdata->p_detail->current_file_format))
			THROW_RML_EXCEPTION(opdata->p_detail->identifier << " already compressed");

		compress(opdata->get_tmp_fstream(),
				opdata->get_tmp_fstream(compressed_format, true, true),
				opdata->p_detail->current_file_format,
				opdata->p_detail->identifier);

		opdata->get_tmp_fstream()->close();
		opdata->p_detail->current_file_format = compressed_format;
	}

	inline_ static void compress(
			rml::utils::tmp_fstream::pointer current_fstream,
			rml::utils::tmp_fstream::pointer compressed_fstream,
			const types::file_format::file_format_t& current_file_format,
			const std::string& identifier)
	{
		if(types::file_format::is_compressed(current_file_format))
			THROW_RML_EXCEPTION(identifier << " already compressed");

		if(!compressed_fstream)
			throw rml::exception("Invalid output stream");

		types::header_precrypt precrypt_hdr;
		precrypt_hdr.inflated_size = current_fstream->get_size();
		// precrypt_hdr.content_length = LATER
		precrypt_hdr.format = types::data_format::compressed;
		precrypt_hdr.sub_format = (current_file_format & 0xff);
		{
			current_fstream->seekg(0);

			CryptoPP::CRC32 hash;
			uint32_t crc32;
			CryptoPP::FileSource(*current_fstream, true,
					new CryptoPP::HashFilter(hash,
							new CryptoPP::ArraySink((byte *)&crc32, CryptoPP::CRC32::DIGESTSIZE)));

			precrypt_hdr.crc32 = crc32;
		}

		if(!(*current_fstream) && !current_fstream->eof())
			THROW_RML_EXCEPTION(identifier << " Error while creating hash [crc32]");

		current_fstream->clear(); // reset eof
		current_fstream->seekg(0); // reset pos

		compressed_fstream->write(reinterpret_cast<char*>(&precrypt_hdr), sizeof(types::header_precrypt));

		// compress and set precrypt_hdr.content_length
		precrypt_hdr.content_length = compress(*current_fstream, *compressed_fstream);

		// rewrite header
		compressed_fstream->seekp(0);
		compressed_fstream->write(reinterpret_cast<char*>(&precrypt_hdr), sizeof(types::header_precrypt));

		//compressed_fstream->set_persistent(true);
	}


	/**
	 * Decompress content_length bytes from is into os
	 *
	 * @param is
	 * @param os
	 * @param content_length
	 * @return number of bytes inserted into os
	 */
	static size_t decompress(std::istream& is, std::ostream& os, const size_t content_length = 0) {
		try {

			std::streampos start = os.tellp();
			{
				bool ostr_failure(false);
				bool istr_failure(false);
				boost::iostreams::filtering_streambuf<boost::iostreams::output> filters;
				filters.push(boost::iostreams::bzip2_decompressor());
				filters.push(internal::safe_ostream_sink(os, ostr_failure));

				if(content_length > 0) {
					size_t copied_length = 0;
					while(!ostr_failure && (copied_length < content_length)) {
						DevDebug("copied_length:"<<copied_length<<" < content_length:"<<content_length);
						size_t buf_length = std::min<size_t>(content_length - copied_length, 8196);
						std::string buf;
						buf.reserve(buf_length);

						is.read(&buf[0], buf_length);
						buf_length = is.gcount();

						if(!buf_length)
							throw std::ios::failure("decompress : Reading from input stream failed");

						boost::iostreams::write(filters, &buf[0], buf_length);

						copied_length += buf_length;
					}
				}
				else {
					/*std::streamsize copied = */boost::iostreams::copy(
							internal::safe_istream_source(is, istr_failure), filters);
				}

				if (ostr_failure || istr_failure)
					throw std::runtime_error("Failed to compress (stream error)");
			}

			return ( size_t ) ( os.tellp() - start );
		}
		catch(const boost::iostreams::bzip2_error& e) {
			int error = e.error();

			if(error == boost::iostreams::bzip2::data_error)
				throw rml::exception("corrupted compressed data");
			else if(error == boost::iostreams::bzip2::data_error_magic)
				throw rml::exception("the 'magic' sequence 'BZh' missing");
			else if(error == boost::iostreams::bzip2::config_error)
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


	static void decompress(app::operation_data::pointer const& opdata, const types::file_format::file_format_t& decompressed_format)
	{
		if(!types::file_format::is_compressed(opdata->p_detail->current_file_format))
			THROW_RML_EXCEPTION(opdata->p_detail->identifier << " already decompressed");

		rml::utils::tmp_fstream::pointer current_fstream = opdata->get_tmp_fstream();

		rml::utils::tmp_fstream::pointer decompressed_fstream = opdata->get_tmp_fstream(decompressed_format, true, true);
		if(!decompressed_fstream)
			throw rml::exception("Unable to get tmp_fstream");

		current_fstream->seekg(0);

		types::header_precrypt precrypt_hdr;
		current_fstream->read(reinterpret_cast<char *>(&precrypt_hdr), sizeof(types::header_precrypt));

		if(precrypt_hdr.format != types::data_format::compressed)
			THROW_RML_EXCEPTION(opdata->p_detail->identifier<<" not compressed according to precrypt-header");

		std::size_t decompressed_size = decompress(*current_fstream, *decompressed_fstream, precrypt_hdr.content_length);
		if(precrypt_hdr.inflated_size != decompressed_size)
			THROW_RML_EXCEPTION(opdata->p_detail->identifier<<" file size mismatch");

		decompressed_fstream->seekg(0);

		uint32_t crc32;
		{
			CryptoPP::CRC32 hash;
			CryptoPP::FileSource f(*decompressed_fstream, true,
					new CryptoPP::HashFilter(hash,
							new CryptoPP::ArraySink((byte *)&crc32, CryptoPP::CRC32::DIGESTSIZE)));
		}

		if(crc32 != precrypt_hdr.crc32)
			THROW_RML_EXCEPTION(opdata->p_detail->identifier<<" CRC mismatch");

		opdata->get_tmp_fstream()->close();
		opdata->p_detail->current_file_format = decompressed_format;
	}
};

}  // namespace app

#endif /* COMPRESS_HPP_ */
