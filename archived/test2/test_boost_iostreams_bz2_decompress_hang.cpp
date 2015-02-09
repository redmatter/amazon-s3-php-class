/*
 * test_boost_iostreams_bz2_decompress_hang.cpp
 *
 *  Created on: Dec 3, 2013
 *      Author: dkorah
 */
#include <rml/logger.h>
#include <gtest/gtest.h>

#include "../compress.hpp"
#include "../crypto.hpp"

int version_number[4];

TEST(DiskFull, DeCompressThrows)
{
	{
		bool failure(false);
		std::ifstream ifs("file.bz2");
		std::ofstream ofs("/dev/full");
		ofs.exceptions(std::ios::badbit);

		boost::iostreams::filtering_streambuf<boost::iostreams::output> filters;
		filters.push(boost::iostreams::bzip2_decompressor());
		filters.push(internal::safe_ostream_sink(ofs, failure));
		ASSERT_THROW(boost::iostreams::copy(ifs, filters), std::ios::failure);
		ASSERT_EQ(true, failure);
	}

	{
		bool failure(false);
		std::ifstream ifs("file.plain");
		std::ofstream ofs("/dev/full");
		ofs.exceptions(std::ios::badbit);

		boost::iostreams::filtering_streambuf<boost::iostreams::output> filters;
		filters.push(boost::iostreams::bzip2_compressor());
		filters.push(internal::safe_ostream_sink(ofs, failure));
		ASSERT_THROW(boost::iostreams::copy(ifs, filters), std::ios::failure);
		ASSERT_EQ(true, failure);
	}
}


TEST(DiskFull, CryptoThrows)
{
	CryptoPP::AutoSeededRandomPool rng;
	boost::array<uint8_t, 32> key;

	rng.GenerateBlock(&key[0], 32);

	app::crypto crypto;

	{
		std::ifstream ifs("file.bz2");
		std::ofstream ofs("/dev/full");
		ASSERT_THROW(crypto.encrypt(ifs, ofs, key), std::exception);
	}

	{
		std::ifstream ifs("file.bz2");
		std::ofstream ofs("file.bz2-bf");
		crypto.encrypt(ifs, ofs, key);
	}

	{
		std::ifstream ifs("file.bz2-bf");
		std::ofstream ofs("/dev/full");
		ASSERT_THROW(crypto.decrypt(ifs, ofs, key), std::exception);
	}

	{
		std::ifstream ifs("file.bz2-bf");
		std::ofstream ofs("file.bz2-bf.plain");
		crypto.decrypt(ifs, ofs, key);
	}
}


TEST(Corruption, GetFailure)
{
	// open the bad file
	rml::utils::tmp_fstream::pointer compressed_with_headers =
			rml::utils::tmp_fstream::create(
					"/media/a411458f-874b-4f92-a71c-6084f6106fc3/dkorah/bad.wav.bz2",
					std::ios_base::in | std::ios_base::out | std::ios_base::binary);

	// read off the header and print it
	app::types::header_precrypt precrypt_hdr;
	compressed_with_headers->read(reinterpret_cast<char *>(&precrypt_hdr), sizeof(app::types::header_precrypt));

	LogDebug("precrypt-header : magic          "<<(char)precrypt_hdr.magic[0]<<(char)precrypt_hdr.magic[1]<<(char)precrypt_hdr.magic[2]);
	LogDebug("precrypt-header : format         "<<(int)precrypt_hdr.format);
	LogDebug("precrypt-header : sub_format     "<<(int)precrypt_hdr.sub_format);
	LogDebug("precrypt-header : content_length "<<precrypt_hdr.content_length);
	LogDebug("precrypt-header : inflated_size  "<<precrypt_hdr.inflated_size);
	LogDebug("precrypt-header : crc32          "<<precrypt_hdr.crc32);

	// pass in for decompression

	std::ofstream ofs("/media/a411458f-874b-4f92-a71c-6084f6106fc3/dkorah/bad.wav");
	ofs.exceptions(std::ios::badbit);

//	boost::iostreams::filtering_streambuf<boost::iostreams::output> filters;
//	filters.push(boost::iostreams::bzip2_decompressor());
//	filters.push(internal::safe_ostream_sink(ofs, failure));
//	ASSERT_THROW(boost::iostreams::copy(*compressed_with_headers, filters), std::ios::failure);
//	ASSERT_EQ(true, failure);

	app::bzip2::decompress(*compressed_with_headers, ofs, precrypt_hdr.content_length);
}
