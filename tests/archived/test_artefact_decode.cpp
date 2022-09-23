//
// Created by dkorah on 20/09/22.
//
#include "gtest/gtest.h"

#include "../../common/artefact/decoder_streambuf.hpp"

#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/bzip2.hpp>

#include <unistd.h>

#if defined(WIN32) || defined(_WIN32)
#define PATH_SEPARATOR "\\"
#else
#define PATH_SEPARATOR "/"
#endif

std::string
get_data_dir()
{
    std::string file(__FILE__);
    return file.substr(0, file.rfind(PATH_SEPARATOR)) + PATH_SEPARATOR "data";
}

bool
DecodesOK(const std::string& enc_file)
{
    namespace ns = common::artefact;
    namespace mpl = boost::mpl;

    static std::string data_dir = get_data_dir();

    std::cout << "data_dir: " << data_dir << std::endl;

    const std::string in_file = data_dir + PATH_SEPARATOR + enc_file;
    const std::string out_file = data_dir + PATH_SEPARATOR + enc_file + ".decoded";

    std::ifstream ifs(in_file.c_str(), std::ios::binary | std::ios::in);
    if (!ifs) {
        throw std::runtime_error(in_file + ": cannot open input");
    }

    unlink(out_file.c_str());
    std::ofstream ofs(out_file.c_str(), std::ios::binary | std::ios::out);
    if (!ofs) {
        throw std::runtime_error(out_file + ": cannot open output");
    }

    ns::key_type k;
    // clang-format off
    CryptoPP::StringSource ss("7BCSLuUcDPpd1bKDRX998fF3Nw0xF0kedHsjDRiCZdo=", true,
                              new CryptoPP::Base64Decoder(
                                  new CryptoPP::ArraySink(&k[0], ns::key_type::size())));
    // clang-format on

    ns::decoder_streambuf dec(k, ofs);
    dec.set_auto_close(true);
    boost::iostreams::copy(ifs, dec);
    boost::iostreams::close(dec);

    return dec.options.sanity_check_ok();
}

TEST(Decode, Everything)
{
    ASSERT_NO_THROW(ASSERT_TRUE(DecodesOK("test-encrypted-compressed-v2.wav.enc")););
    ASSERT_NO_THROW(ASSERT_TRUE(DecodesOK("test-encrypted-not-compressed-v2.mp3.enc")););
    ASSERT_NO_THROW(ASSERT_TRUE(DecodesOK("test-encrypted-compressed-v1.wav.enc")););
    ASSERT_NO_THROW(ASSERT_TRUE(DecodesOK("test-encrypted-not-compressed-v1.mp3.enc")););
    ASSERT_NO_THROW(ASSERT_TRUE(DecodesOK("sample.wav")););
    ASSERT_NO_THROW(ASSERT_TRUE(DecodesOK("sample.mp3")););
}
