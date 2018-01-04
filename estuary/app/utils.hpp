//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 13/12/2017.
//

#ifndef ARCHIVING_ESTUARY_APP_UTILS_HPP
#define ARCHIVING_ESTUARY_APP_UTILS_HPP

#include <cryptopp/hex.h>
#include <cryptopp/base64.h>

#include <string>

namespace app
{
namespace utils
{

std::string base64_to_hex(const std::string& base64)
{
    std::string decoded_hex;

    CryptoPP::StringSource ss(base64, true,
                              new CryptoPP::Base64Decoder(
                                      new CryptoPP::HexEncoder(
                                              new CryptoPP::StringSink(decoded_hex), false)));

    return decoded_hex;
}

} // namespace utils
} // namespace app
#endif //ARCHIVING_ESTUARY_APP_UTILS_HPP
