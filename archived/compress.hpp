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

				boost::iostreams::filtering_streambuf<boost::iostreams::output> filters;
				filters.push(boost::iostreams::bzip2_compressor());
				filters.push(os);

				if(content_length > 0) {
					size_t copied_length = 0;
					while(copied_length < content_length) {
						size_t buf_length = std::min<size_t>(content_length - copied_length, 8196);

						std::vector<char> buf(buf_length);

						is.read(&buf[0], buf_length);
						buf_length = is.gcount();
						boost::iostreams::write(filters, &buf[0], buf_length);

						copied_length += buf_length;
					}
				}
				else {
					/*std::streamsize copied = */boost::iostreams::copy(is, filters);
				}
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

		try {
			compress(opdata->get_tmp_fstream(),
					opdata->get_tmp_fstream(compressed_format, true, true),
					opdata->p_detail->current_file_format,
					opdata->p_detail->identifier);
		}
		catch(...) {
			// TODO device an auto cleanup of the compressed_format; this will only close the file handle, still leaves file around till we either retry or shutdown process
			opdata->get_tmp_fstream()->close();
			opdata->get_tmp_fstream(compressed_format)->close();
			throw;
		}

		opdata->get_tmp_fstream()->close();
		opdata->p_detail->current_file_format = compressed_format;
	}

	inline static void compress(
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
				boost::iostreams::filtering_streambuf<boost::iostreams::output> filters;
				filters.push(boost::iostreams::bzip2_decompressor());
				filters.push(os);

				if(content_length > 0) {
					size_t copied_length = 0;
					while(copied_length < content_length) {
						size_t buf_length = std::min<size_t>(content_length - copied_length, 8196);
						std::string buf;
						buf.reserve(buf_length);

						is.read(&buf[0], buf_length);
						buf_length = is.gcount();

						boost::iostreams::write(filters, &buf[0], buf_length);

						copied_length += buf_length;
					}
				}
				else {
					/*std::streamsize copied = */boost::iostreams::copy(is, filters);
				}
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

	app::operation_data::channel_t& compress_channel_;
	app::operation_data::channel_t& main_channel_;
	volatile bool& quit_flag_;

public:
	bzip2(app::operation_data::channel_t& compress_channel, app::operation_data::channel_t& main_channel, volatile bool& quit_flag) :
		compress_channel_(compress_channel), main_channel_(main_channel), quit_flag_(quit_flag)
	{
	}

	void run()
	{
		while(!quit_flag_) {
			try {
				operation_data::pointer opdata = compress_channel_.consume();

				if(opdata->p_detail->state != operation_state::get_file_convert || opdata->p_detail->state != operation_state::put_file_convert);
					THROW_RML_EXCEPTION(opdata->p_detail->identifier << " Invalid call sequence");

				const std::pair<types::file_format::convert_action::convert_action_t, types::file_format::file_format_t>& next(
						types::file_format::get_next(opdata->p_detail->current_file_format, opdata->p_detail->target_file_format));

				switch(next.first) {
				case types::file_format::convert_action::compress:
					compress(opdata, next.second);
					break;

				case types::file_format::convert_action::uncompress:
					decompress(opdata, next.second);
					break;

				default:
					THROW_RML_EXCEPTION(opdata->p_detail->identifier << " Invalid call sequence");
				}

				{
					const std::pair<types::file_format::convert_action::convert_action_t, types::file_format::file_format_t>& next(
							types::file_format::get_next(opdata->p_detail->current_file_format, opdata->p_detail->target_file_format));

					if(next.first == types::file_format::convert_action::nothing) // we dont care if the format is good; at least no infinite loop here!
						opdata->p_detail->state = operation_state::next_up(opdata->p_detail->state); // let it move on from "convert"
				}

				// put it back to the main channel
				main_channel_.produce(opdata);
			}
			catch(const app::operation_data::channel_t::finishing_exception& e) {
				LogMessage("Channel(s) finishing");
				break;
			}
			catch(const std::exception& e) {
				LogError("exception: "<<e.what());
			}
			catch(...) {
				LogError("Unexpected exception");
			}
		}
	}
};

}  // namespace app

#endif /* COMPRESS_HPP_ */
