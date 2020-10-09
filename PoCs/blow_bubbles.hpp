//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 30/09/2020.
//

#ifndef ARCHIVING_BLOW_BUBBLES_HPP
#define ARCHIVING_BLOW_BUBBLES_HPP

#include <boost/thread.hpp>
namespace poc {

void
blow_bubbles_till(bool& condition)
{
    static const char* sprite = ".     o     O     @     *          ";
    unsigned frame = 0;
    while (!condition) {
        boost::this_thread::sleep_for(boost::chrono::milliseconds(250));
        std::cerr << "\r‗‗" << std::string(sprite + frame * 5, 5) << ' ';
        frame = (frame + 1) % 7;
    }
    std::cerr << std::endl;
}

void
blow_bubbles()
{
    bool eternity = false;
    blow_bubbles_till(eternity);
}

} // namespace poc

#endif // ARCHIVING_BLOW_BUBBLES_HPP
