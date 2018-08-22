//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 10/01/2018.
//

#ifndef ARCHIVING_PUT_REQUEST_HPP
#define ARCHIVING_PUT_REQUEST_HPP

#include "../../common/types.hpp"
#include "../../common/policy.hpp"
#include "error.hpp"
#include "utils.hpp"
#include "archivable_queue.hpp"
#include "http/pending_request.hpp"
#include "../../archived/types.hpp"

#include <boost/shared_ptr.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/optional.hpp>
#include <boost/regex.hpp>

#include <iostream>

namespace app
{

struct put_object_request
{
    const boost::optional<archiving::file_type::file_type_t> file_type;
    const boost::optional<archiving::org_id_t> org_id;
    const boost::optional<boost::uuids::uuid> uuid;

    const boost::optional<time_t> creation_time;
    const boost::optional<app::policy::id_type> policy_id;
    const boost::optional<std::string> pbx_hostname;
    const boost::optional<boost::uuids::uuid> leg_uuid;
    const boost::optional<archiving::user_id_t> user_id;

    const boost::optional<size_t> content_length;
    const boost::optional<std::string> content_type;
    const boost::optional<std::string> content_md5;
    const boost::optional<std::string> accept;

    mutable boost::shared_ptr<rml::utils::tmp_fstream> fstream;
    mutable size_t bytes_remaining;

    const http::request_progress_callback_type progress_callback;

    std::string get_pbx_hostname() const
    {
        if (pbx_hostname == boost::none) {
            return "unknown";
        }

        return *pbx_hostname;
    }

    static inline
    boost::optional<std::string> base64_to_hex(const boost::optional<std::string>& base64)
    {
        if (base64 == boost::none || (*base64).empty()) {
            return boost::none;
        }

        try {
            const std::string& hex(utils::base64_to_hex(*base64));
            if (!hex.empty()) {
                return hex;
            }
        } catch (const CryptoPP::Exception& e) {
            LogVerbose("Failed to decode Content-MD5: " << *base64 << " Error:" << e.what());
        }

        return boost::none;
    }

    boost::shared_ptr<std::iostream> get_iostream() const
    {
        return boost::static_pointer_cast<std::iostream>(fstream);
    }

    put_object_request(const http::pending_request& req, const http::request_progress_callback_type& progress_callback) :
            file_type(req.request.get_path_vector().get_component<archiving::file_type::file_type_t>(1, "file-type")),
            org_id(req.request.get_path_vector().get_component<archiving::org_id_t>(2, "organisation-id")),
            uuid(req.request.get_path_vector().get_component<boost::uuids::uuid>(3, "uuid")),

            creation_time(req.request.get_query_map().get_first_value_as<time_t>("time")),
            policy_id(req.request.get_query_map().get_first_value_as<app::policy::id_type>("policy")),
            pbx_hostname(req.request.get_query_map().get_first_value_as<std::string>("pbx")),
            leg_uuid(req.request.get_query_map().get_first_value_as<boost::uuids::uuid>("legUuid")),
            user_id(req.request.get_query_map().get_first_value_as<archiving::user_id_t>("user")),

            content_length(req.request.get_first_header_value_as<size_t>("Content-Length")),
            content_type(req.request.get_first_header_value_as<std::string>("Content-Type")),
            content_md5(base64_to_hex(req.request.get_first_header_value_as<std::string>("Content-MD5"))),
            accept(req.request.get_first_header_value_as<std::string>("Accept")),

            bytes_remaining(content_length != boost::none ? *content_length : 0),

            progress_callback(progress_callback)
    {
        if (req.request.get_method() != "PUT") {
            boost::throw_exception(
                    boost::enable_error_info(error::http_error_exception())
                            << error::error_info_request_correlation(req.correlation)
                            << error::error_info_error_condition(
                                    boost::system::error_condition(error::http::method_not_allowed)));
        }

        if (accept != boost::none &&
            !boost::regex_search(*accept, boost::regex("(?:^|, )(?:application/json|\\*/\\*)(?=$|[;,])"))) {

            boost::throw_exception(
                    boost::enable_error_info(error::http_error_exception("JSON not acceptable"))
                            << error::error_info_request_correlation(req.correlation)
                            << error::error_info_error_condition(
                                    boost::system::error_condition(error::http::not_acceptable)));
        }

        if (file_type == boost::none || org_id == boost::none || uuid == boost::none) {
            boost::throw_exception(
                    boost::enable_error_info(error::http_error_exception("Unexpected path-info in request"))
                            << error::error_info_request_correlation(req.correlation)
                            << error::error_info_error_condition(
                                    boost::system::error_condition(error::http::bad_request)));
        }

        if (content_length == boost::none) {
            boost::throw_exception(
                    boost::enable_error_info(error::http_error_exception("Content-Length not given"))
                            << error::error_info_request_correlation(req.correlation)
                            << error::error_info_error_condition(
                                    boost::system::error_condition(error::http::length_required)));
        }

        if (content_length <= static_cast<size_t>(0)) {
            boost::throw_exception(
                    boost::enable_error_info(error::http_error_exception("Content-Length invalid"))
                            << error::error_info_request_correlation(req.correlation)
                            << error::error_info_error_condition(
                                    boost::system::error_condition(error::http::bad_request)));
        }

        if (content_type == boost::none || (*content_type).empty()) {
            boost::throw_exception(
                    boost::enable_error_info(error::http_error_exception("Content-Type invalid"))
                            << error::error_info_request_correlation(req.correlation)
                            << error::error_info_error_condition(
                                    boost::system::error_condition(error::http::bad_request)));
        }

        if (content_md5 == boost::none) {
            boost::throw_exception(
                    boost::enable_error_info(error::http_error_exception("Content-MD5 invalid"))
                            << error::error_info_request_correlation(req.correlation)
                            << error::error_info_error_condition(
                                    boost::system::error_condition(error::http::bad_request)));
        }

        if (policy_id == boost::none) {
            boost::throw_exception(
                    boost::enable_error_info(error::http_error_exception("Policy-ID not given"))
                            << error::error_info_request_correlation(req.correlation)
                            << error::error_info_error_condition(
                                    boost::system::error_condition(error::http::bad_request)));
        }

        if (creation_time == boost::none) {
            boost::throw_exception(
                    boost::enable_error_info(error::http_error_exception("Creation-Time not specified"))
                            << error::error_info_request_correlation(req.correlation)
                            << error::error_info_error_condition(
                                    boost::system::error_condition(error::http::bad_request)));
        }

        switch (*file_type)
        {
        case archiving::file_type::cdr:
        case archiving::file_type::pcap:
        case archiving::file_type::pcap_sip_only:
            if (*org_id != 1) {
                boost::throw_exception(
                        boost::enable_error_info(error::http_error_exception("Internal file-type must belong to Org 1"))
                                << error::error_info_request_correlation(req.correlation)
                                << error::error_info_error_condition(
                                        boost::system::error_condition(error::http::bad_request)));
            }
            break;
        default:
            break;
        }

        try {
            const std::string& path(
                    boost::str(boost::format("%1%_%2%_%3%_%4%_")
                               % boost::io::group(std::hex, std::setw(16), std::setfill('0'), req.correlation.id)
                               % archiving::file_type::to_string(*file_type) % *org_id % *uuid));
            fstream = rml::utils::tmp_fstream::create(path, *content_length, "/tmp");
        } catch (const rml::utils::tmp_fstream::error& e) {
            if (std::strcmp("Failed to reserve space", e.what()) != 0) {
                throw;
            }

            boost::throw_exception(
                    boost::enable_error_info(error::http_error_exception(e.what()))
                            << error::error_info_request_correlation(req.correlation)
                            << error::error_info_error_condition(
                                    boost::system::error_condition(error::http::insufficient_storage)));
        }
    }

    void enqueue_archivable(
            const bool ok,
            const boost::function<void(const boost::shared_ptr<archiving::archivable>)>& enqueue_callback) const
    {
        if (!ok) {
            return;
        }

        progress_callback(http::request_progress::put_read_data);

        fstream->close();

        // prepare the archivable object
        boost::shared_ptr<archiving::archivable> archivable(
                new archiving::archivable(
                        boost::lexical_cast<std::string>(*uuid),
                        "" /* originator uuid */,
                        *(creation_time),
                        fstream->get_filename(),
                        *(file_type),
                        app::types::data_sub_format::to_string(
                                app::types::data_sub_format::from_mime_type(*content_type)),
                        get_pbx_hostname(),
                        *(policy_id),
                        false,
                        false,
                        "" /* modified file path */,
                        *(content_md5),
                        archiving::archivable::state::collected));

        if (*file_type == archiving::file_type::cdr) {
            archivable->set_add_to_cdrmunch_cache(true);
        }

        if (leg_uuid != boost::none) {
            archivable->set_leg_uuid(boost::lexical_cast<std::string>(*leg_uuid));
        }

        boost::shared_ptr<archiving::owner_info> owners = boost::make_shared<archiving::owner_info>(*org_id);
        if (user_id != boost::none && *user_id > 0) {
            owners->addUser(*user_id);
        }

        archivable->set_owners(*owners);

        enqueue_callback(archivable);

        progress_callback(http::request_progress::put_enqueued);
    }
};

} // namespace app

#endif //ARCHIVING_PUT_REQUEST_HPP
