//
// Created by dkorah on 21/09/22.
//

#ifndef ARCHIVING_DECRYPT_HPP
#define ARCHIVING_DECRYPT_HPP

#include "decode_options.hpp"
#include "pass_through.hpp"

#include <cryptopp/filters.h>

namespace common {
namespace artefact {
namespace detail {

struct decrypter_impl
{
    typedef char char_type;

    bool eof;
    bool initialised;
    const decode_options& options;

    boost::shared_ptr<CryptoPP::StreamTransformation> decryption;
    boost::shared_ptr<CryptoPP::StreamTransformationFilter> decrypter;

    explicit decrypter_impl(const decode_options& options) : eof(false), initialised(false), options(options) {}

    bool filter(const char*& begin_in, const char* end_in, char*& begin_out, char* end_out, bool flush)
    {
        // we cannot initialise this during construction since decode-options wouldn't be finalised till we get the
        // first chunk of data through here.
        if (!initialised) {
            decryption = options.get_decryption_algorithm();
            if (decryption) {
                decrypter.reset(new CryptoPP::StreamTransformationFilter(*decryption, 0));
            }

            initialised = true;
        }

        // if no decryption set, pass through data
        if (!decryption) {
            return pass_through(begin_in, end_in, begin_out, end_out, flush);
        }

        while (true) {
            std::size_t put = 0;
            std::size_t got = 0;

            if (begin_in != end_in) {
                std::size_t put_remaining = decrypter->Put(reinterpret_cast<const byte*>(begin_in), end_in - begin_in);
                put = (end_in - begin_in) - put_remaining;
                begin_in += put;
            }

            if (begin_out != end_out && decrypter->AnyRetrievable()) {
                got = decrypter->Get(reinterpret_cast<byte*>(begin_out), end_out - begin_out);
                begin_out += got;
            }

            // if nothing happened in the loop, break-out
            if (put == 0 && got == 0) {
                break;
            }
        }

        if (flush) {
            if (begin_in == end_in && !eof) {
                decrypter->MessageEnd();
                decrypter->Flush(true);
                eof = true;
            }

            while (begin_out != end_out && decrypter->AnyRetrievable()) {
                begin_out += decrypter->Get(reinterpret_cast<byte*>(begin_out), end_out - begin_out);
            }

            return decrypter->AnyRetrievable();
        }

        return false;
    }

    void close() {}
};

} // namespace detail
} // namespace artefact
} // namespace common

#endif // ARCHIVING_DECRYPT_HPP
