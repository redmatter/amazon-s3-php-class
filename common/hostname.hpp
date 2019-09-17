//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 06/09/2018.
//

#ifndef ARCHIVING_HOSTNAME_HPP
#define ARCHIVING_HOSTNAME_HPP

#include <string>
#include <string.h>
#include <unistd.h>

namespace archiving
{
namespace common
{
std::string get_hostname()
{
    char host[256];
    if (-1 != gethostname(host, 255)) {
        void* dot = memchr(host, '.', 255);
        if (dot != NULL) {
            *static_cast<char*>(dot) = '\0';
        } else {
            host[255] = '\0';
        }
    } else {
        host[255] = '\0';
    }

    return host;
}
} // namespace common
} // namespace archiving

#endif //ARCHIVING_HOSTNAME_HPP
