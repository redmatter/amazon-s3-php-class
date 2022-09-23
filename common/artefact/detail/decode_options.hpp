//
// Created by dkorah on 15/09/22.
//

#ifndef ARCHIVING_DECODE_OPTIONS_HPP
#define ARCHIVING_DECODE_OPTIONS_HPP

#include "../../../archived/file_format/headers.hpp"
#include "../../../archived/types.hpp"
#include "artefact/types.hpp"

#include <stdint.h>

#include <boost/array.hpp>
#include <boost/make_shared.hpp>

namespace common {
namespace artefact {
namespace detail {

struct decode_options
{
    typedef archiving::archived::file_format::crypto_header_union crypto_header_type;
    typedef app::types::header_precrypt precrypt_header_type;

    boost::optional<crypto_header_type> outer_header;
    boost::optional<precrypt_header_type> inner_header;

    key_type encryption_key;
    boost::optional<bool> sanity_check_result;

    bool is_encrypted() const { return outer_header.is_initialized(); }

    bool is_compressed() const
    {
        if (!inner_header.is_initialized()) {
            return false;
        }

        return inner_header->format == app::types::data_format::compressed;
    }

    explicit decode_options(const key_type& k)
        : outer_header(boost::none), inner_header(boost::none), encryption_key(k), sanity_check_result(boost::none)
    {
    }

    void set_outer_header(const crypto_header_type& h) { outer_header = h; }

    inline bool has_outer_header() const { return outer_header.is_initialized(); }

    void set_inner_header(const precrypt_header_type& h) { inner_header = h; }

    inline bool has_inner_header() const { return inner_header.is_initialized(); }

    void set_sanity_check_result(bool result) { sanity_check_result = result; }

    bool sanity_check_ok() const
    {
        // In cases where there is no inner header or no header at all, sanity check
        // will not take place. In such cases check-result is deemed OK since there is
        // no way to check.
        return sanity_check_result == boost::none || *sanity_check_result;
    }

    uint8_t get_version() const
    {
        if (!outer_header.is_initialized()) {
            return 0;
        }

        return outer_header->format_version.major;
    }

    inline boost::shared_ptr<CryptoPP::StreamTransformation> get_decryption_algorithm() const
    {
        using namespace archiving::archived::file_format;

        switch (get_version()) {
        case 0:
            return boost::shared_ptr<CryptoPP::StreamTransformation>();
        case 1:
            return boost::make_shared<crypto_header<version<1> >::encryption_algorithm_type::cipher_mode::Decryption>(
                &encryption_key[0], key_type::size(), outer_header->v1.iv);
        case 2:
            return boost::make_shared<crypto_header<version<2> >::encryption_algorithm_type::cipher_mode::Decryption>(
                &encryption_key[0], key_type::size(), outer_header->v2.iv);
        default:
            throw std::runtime_error("unknown header version");
        }
    }
};

} // namespace detail
} // namespace artefact
} // namespace common

#endif // ARCHIVING_DECODE_OPTIONS_HPP
