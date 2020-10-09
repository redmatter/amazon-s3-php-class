//
// Copyright (c) 2020 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 30/09/2020.
//

#ifndef ARCHIVED_NET_PUT_REQUEST_HPP
#define ARCHIVED_NET_PUT_REQUEST_HPP

#include <md5.hpp>
#include <policy.hpp>

#include "../../types.hpp"
#include "exception.hpp"

#include <boost/algorithm/hex.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/shared_ptr.hpp>

namespace archiving {
namespace archived {
namespace net {
namespace put {
struct request
{
    typedef app::types::metadata metadata_type;
    typedef app::policy::id_type policy_id_type;
    typedef app::types::data_sub_format::data_sub_format_t sub_format_type;

    const archiving::file_type::file_type_t file_type;
    const archiving::org_id_t org_id;
    const std::string uuid;
    const std::size_t content_length;
    const boost::shared_ptr<const metadata_type> metadata;

    const std::string storage_key;
    const std::time_t creation_time;
    const archiving::hash_vector md5;
    const policy_id_type policy_id;
    const sub_format_type sub_format;

private:
    const std::string cache_dir;
    boost::shared_ptr<rml::utils::tmp_fstream> file;

public:
    request(archiving::file_type::file_type_t file_type,
            archiving::org_id_t org_id,
            const std::string& uuid,
            long content_length,
            const boost::shared_ptr<const metadata_type>& metadata,
            const std::string& cache_dir)
        : file_type(file_type)
        , org_id(org_id)
        , uuid(uuid)
        , content_length(content_length)
        , metadata(metadata)

        , storage_key(make_storage_key())
        , creation_time(get_metadata<std::time_t>("CreateTime"))
        , md5(hex_decode_md5())
        , policy_id(get_metadata<policy_id_type>("RetentionPolicyID"))
        , sub_format(app::types::data_sub_format::from_string(get_metadata<std::string>("DataFormat")))
        , cache_dir(cache_dir)
    {}

    ~request()
    {
        if (file && file->is_open()) {
            file->close();
        }
    }

    boost::shared_ptr<rml::utils::tmp_fstream> get_file()
    {
        if (!file) {
            make_file();
        }

        return file;
    }

    void make_file()
    {
        std::string time_component;
        {
            char str[16];
            struct tm* t = localtime(&creation_time);
            strftime(str, 16, "%Y%m%dT%H00", t);

            time_component = str;
        }

        file = rml::utils::tmp_fstream::create(time_component + "/" + storage_key + ".put.", content_length, cache_dir);
    }

    void finalise()
    {
        if (!file) {
            throw internal_error("no file received");
        }

        file->re_open();

        const archiving::hash_vector& md5sum = archiving::md5hasher::hash(*file);
        if (md5 != md5sum) {
            throw bad_request("MD5 did not match");
        }

        file->close();
    }

private:
    template<typename T>
    inline boost::optional<T> optional_metadata(const std::string& key) const
    try {
        if (!metadata) {
            return boost::none;
        }

        metadata_type::const_iterator itr = metadata->find(key);
        if (itr == metadata->end()) {
            return boost::none;
        }

        return boost::lexical_cast<T>(itr->second);
    } catch (const boost::bad_lexical_cast& e) {
        LogDWarning(e.what(), "IGNO");
        throw bad_request(key + " has no/invalid value");
    }

    template<typename T>
    inline T get_metadata(const std::string& key, const T& default_value) const
    {
        const boost::optional<T>& value = optional_metadata<T>(key);
        if (value != boost::none) {
            return *value;
        }

        return default_value;
    }

    template<typename T>
    inline T get_metadata(const std::string& key) const
    {
        const boost::optional<T>& value = optional_metadata<T>(key);
        if (value != boost::none) {
            return *value;
        }

        throw bad_request(key + " not found in metadata");
    }

    std::string make_storage_key() const
    {
        std::ostringstream os;
        os << file_type << '/' << org_id << '/' << uuid;
        return os.str();
    }

    archiving::hash_vector hex_decode_md5() const
    {
        const std::string& str = get_metadata<std::string>("MD5");

        std::vector<archiving::hash_vector::value_type> v;
        boost::algorithm::unhex(str.begin(), str.end(), std::back_inserter(v));
        if (v.size() != archiving::hash_vector::size()) {
            throw bad_request("Bad MD5 value");
        }

        archiving::hash_vector hash;
        boost::range::copy(v, hash.begin());

        return hash;
    }
};
} // namespace put
} // namespace net
} // namespace archived
} // namespace archiving

#endif // ARCHIVED_NET_PUT_REQUEST_HPP
