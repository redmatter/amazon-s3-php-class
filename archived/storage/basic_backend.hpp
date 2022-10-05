//
// Created by dkorah on 05/10/22.
//

#ifndef ARCHIVING_STORAGE_BASIC_BACKEND_HPP
#define ARCHIVING_STORAGE_BASIC_BACKEND_HPP

#include "../operation_data.hpp"

namespace {

using app::storage::endpoint_type;
using boost::program_options::options_description;
using boost::program_options::value;
using boost::program_options::variables_map;
using app::operation_data;

} // namespace

namespace archiving {
namespace archived {
namespace storage {
struct basic_backend
{
    virtual bool put(const operation_data::pointer& opdata) = 0;
};
} // namespace storage
} // namespace archived
} // namespace archiving

#endif // ARCHIVING_STORAGE_BASIC_BACKEND_HPP
