//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 07/12/2017.
//

#ifndef ARCHIVING_STATE_REACTOR_HPP
#define ARCHIVING_STATE_REACTOR_HPP

#include <rml/state_reactor.hpp>
#include <rml/pool/lockfree_pool.hpp>
#include <boost/unordered_map.hpp>

namespace app
{

typedef rml::state_reactor<> state_reactor_type;
typedef rml::pool::lockfree_pool<int /*worker*/ > worker_pool_type;

typedef boost::function<void(boost::shared_ptr<int /*request*/ >)> uri_handler_type;

#pragma clang diagnostic push
#pragma ide diagnostic ignored "TemplateArgumentsIssues"
typedef boost::unordered_map<std::string, uri_handler_type> uri_handler_map_type;
#pragma clang diagnostic pop

} // namespace app

#endif //ARCHIVING_STATE_REACTOR_HPP
