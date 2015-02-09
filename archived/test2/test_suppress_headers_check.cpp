/*
 * test_boost_iostreams_bz2_decompress_hang.cpp
 *
 *  Created on: Dec 3, 2013
 *      Author: dkorah
 */
#include <rml/logger.h>
#include <gtest/gtest.h>

#include "../suppress_headers_check.hpp"

int version_number[4];
const boost::gregorian::date legacy_end_date(boost::gregorian::from_simple_string("2013-01-01"));
const boost::gregorian::date a_legacy_date(boost::gregorian::from_simple_string("2010-01-01"));
const boost::gregorian::date a_non_legacy_date(boost::gregorian::from_simple_string("2015-01-01"));

TEST(TestSetup, MakeSureGlobalsAreValid)
{
	ASSERT_EQ(false, legacy_end_date.is_not_a_date());
	ASSERT_EQ(false, a_legacy_date.is_not_a_date());
	ASSERT_EQ(false, a_non_legacy_date.is_not_a_date());
}

TEST(HeadersCheckSuppressionTest, ConstructorFailsForInvalidConfigValues)
{
	ASSERT_THROW(
			app::headers_check::suppressions("undefined-scenario", legacy_end_date),
			rml::exception);

	ASSERT_THROW(
			app::headers_check::suppressions("none,,internal-filetype", legacy_end_date),
			rml::exception);

	ASSERT_THROW(
			app::headers_check::suppressions("undefined-scenario,internal-filetype", legacy_end_date),
			rml::exception);

	ASSERT_THROW(
			app::headers_check::suppressions("internal-filetypes,undefined-scenario", legacy_end_date),
			rml::exception);

	ASSERT_THROW(
			app::headers_check::suppressions("internal-filetype,", legacy_end_date),
			rml::exception);

	ASSERT_THROW(
			app::headers_check::suppressions(",", legacy_end_date),
			rml::exception);

	ASSERT_NO_THROW(app::headers_check::suppressions("", legacy_end_date));
	ASSERT_NO_THROW(app::headers_check::suppressions("none", legacy_end_date));
	ASSERT_NO_THROW(app::headers_check::suppressions("any-filetype-with-legacy-headers", legacy_end_date));
}

TEST(HeadersCheckSuppressionTest, EmptyConfig_DisablesSuppression_ForAllScenarios)
{
	app::headers_check::suppressions suppressions("", legacy_end_date);
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::cdr, a_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::recording, a_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::pcap, a_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::pcap_sip_only, a_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::sms, a_legacy_date));
}

TEST(HeadersCheckSuppressionTest, None_DisablesSuppression_ForAllScenarios)
{
	app::headers_check::suppressions suppressions("none", legacy_end_date);
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::cdr, a_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::recording, a_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::pcap, a_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::pcap_sip_only, a_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::sms, a_legacy_date));
}

TEST(HeadersCheckSuppressionTest, InternalFiletypes_EnablesSuppression_ForInternalFiletypesIgnoresCreationDate)
{
	app::headers_check::suppressions suppressions("internal-filetype", legacy_end_date);
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::cdr, a_non_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::recording, a_non_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::pcap, a_non_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::pcap_sip_only, a_non_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::sms, a_non_legacy_date));
}

TEST(HeadersCheckSuppressionTest, AnyFiletypes_EnablesSuppression_ForAllFiletypesIgnoresCreationDate)
{
	app::headers_check::suppressions suppressions("any-filetype", legacy_end_date);
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::cdr, a_non_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::recording, a_non_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::pcap, a_non_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::pcap_sip_only, a_non_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::sms, a_non_legacy_date));
}

TEST(HeadersCheckSuppressionTest, AnyFiletypesWithLegacyHeaders_EnablesSuppressionFor_AllFiletypesWithLegacyHeaders)
{
	app::headers_check::suppressions suppressions("any-filetype-with-legacy-headers", legacy_end_date);

	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::cdr, a_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::recording, a_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::pcap, a_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::pcap_sip_only, a_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::sms, a_legacy_date));

	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::cdr, a_non_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::recording, a_non_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::pcap, a_non_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::pcap_sip_only, a_non_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::sms, a_non_legacy_date));
}

TEST(HeadersCheckSuppressionTest, InternalFiletypesWithLegacyHeaders_EnablesSuppressionFor_InternalFiletypesWithLegacyHeaders)
{
	app::headers_check::suppressions suppressions("internal-filetype-with-legacy-headers", legacy_end_date);

	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::cdr, a_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::recording, a_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::pcap, a_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::pcap_sip_only, a_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::sms, a_legacy_date));

	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::cdr, a_non_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::recording, a_non_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::pcap, a_non_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::pcap_sip_only, a_non_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::sms, a_non_legacy_date));
}

TEST(HeadersCheckSuppressionTest, AnyTypesWithLegacyHeadersAndInternalTypes_EnablesSuppressionFor_AllFiletypesWithLegacyHeadersAsWellAsAnyInternalTypes)
{
	app::headers_check::suppressions suppressions("any-filetype-with-legacy-headers,internal-filetype", legacy_end_date);

	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::cdr, a_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::recording, a_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::pcap, a_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::pcap_sip_only, a_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::sms, a_legacy_date));

	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::cdr, a_non_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::recording, a_non_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::pcap, a_non_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::pcap_sip_only, a_non_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::sms, a_non_legacy_date));
}

TEST(HeadersCheckSuppressionTest, AnyTypesWithLegacyHeadersAndNonInternalTypes_EnablesSuppressionFor_AllFiletypesWithLegacyHeadersAsWellAsAnyNonInternalTypes)
{
	app::headers_check::suppressions suppressions("any-filetype-with-legacy-headers,non-internal-filetype", legacy_end_date);

	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::cdr, a_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::recording, a_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::pcap, a_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::pcap_sip_only, a_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::sms, a_legacy_date));

	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::cdr, a_non_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::recording, a_non_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::pcap, a_non_legacy_date));
	ASSERT_EQ(false, suppressions.is_suppressed(archiving::file_type::pcap_sip_only, a_non_legacy_date));
	ASSERT_EQ(true, suppressions.is_suppressed(archiving::file_type::sms, a_non_legacy_date));
}
