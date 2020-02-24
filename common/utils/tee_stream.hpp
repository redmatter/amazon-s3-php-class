//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 23/02/2020.
//

#ifndef ARCHIVING_TEE_ISTREAM_HPP
#define ARCHIVING_TEE_ISTREAM_HPP

#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/invert.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/tee.hpp>
#include <boost/tr1/memory.hpp>
#include <iostream>

namespace common {
namespace utils {

namespace io = boost::iostreams;

namespace detail {
struct tee_istream : public io::source
{
	tee_istream(std::istream& in, std::ostream& tee_out) : in_(in), tee_(io::tee(tee_out), 1) {}

	std::streamsize read(char* s, std::streamsize n) { return tee_.read(in_, s, n); }

private:
	std::istream& in_;
	io::inverse<io::tee_filter<std::ostream> > tee_;
};

typedef io::tee_device<std::ostream, std::ostream> tee_ostream_device;
typedef io::stream<tee_ostream_device> tee_ostream;

} // namespace detail

struct tee_istream : public io::filtering_istream
{
	tee_istream(std::istream& in, std::ostream& tee_out) : io::filtering_istream(detail::tee_istream(in, tee_out), 0) {}
};

struct tee_ostream : public detail::tee_ostream
{
	tee_ostream(std::ostream& out, std::ostream& tee_out)
		: detail::tee_ostream(detail::tee_ostream_device(out, tee_out))
	{}
};

} // namespace utils
} // namespace common

#endif // ARCHIVING_TEE_ISTREAM_HPP
