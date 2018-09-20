//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 13/09/2017.
//

#ifndef ARCHIVING_ARCHIVED_FILE_FORMAT_POLICIES_HPP
#define ARCHIVING_ARCHIVED_FILE_FORMAT_POLICIES_HPP

#include <boost/mpl/int.hpp>
#include <boost/parameter.hpp>

namespace archiving
{
namespace archived
{
namespace file_format
{

namespace tag { struct version; }

template <unsigned MajorVersion>
struct version : boost::parameter::template_keyword<tag::version, boost::mpl::int_<MajorVersion> >
{};

} // namespace file_format
} // namespace archived
} // namespace archiving

#endif //ARCHIVING_ARCHIVED_FILE_FORMAT_POLICIES_HPP
