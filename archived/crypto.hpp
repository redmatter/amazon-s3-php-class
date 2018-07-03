/*
 * crypto.hpp
 *
 *  Created on: 2 Feb 2011
 *      Author: dkorah
 */

#ifndef CRYPTO_HPP_
#define CRYPTO_HPP_

#include "types.hpp"
#include "operation_data.hpp"
#include "storage_endpoint.hpp"
#include "file_format/headers.hpp"

#include <rml/utils.h>

#include <cryptopp/osrng.h>
#include <cryptopp/cryptlib.h>
#include <cryptopp/hex.h>
#include <cryptopp/filters.h>
#include <cryptopp/blowfish.h>
#include <cryptopp/modes.h>
#include <cryptopp/secblock.h>
#include <cryptopp/files.h>
#include <cryptopp/crc.h>

#include <string.h>

namespace app {

struct unknown_file_format_exception : rml::exception
{
	unknown_file_format_exception(const int current_version_major, const int current_version_minor,
								  const int found_version_major, const int found_version_minor) :
			rml::exception(boost::str(
					boost::format("Unknown file-format found; current-version: v%1%.%2% found-version: v%3%.%4%") %
					current_version_major % current_version_minor % found_version_major % found_version_minor))
	{}
};

class crypto
{
	CryptoPP::AutoSeededRandomPool rng_;

public:
//	inline void encrypt(std::istream& istr, std::ostream& ostr, operation_data::detail::pointer const& detail)
//	{
//		encrypt(istr, ostr, detail->p_endpoint->encryption_key);
//	}

	void encrypt(std::istream& istr, std::ostream& ostr, const storage::basic_endpoint::encryption_key_t& encryption_key)
	{
		try {
			uint8_t iv[CryptoPP::Blowfish::BLOCKSIZE];
			rng_.GenerateBlock(iv, sizeof(iv));

			CryptoPP::CBC_Mode< CryptoPP::Blowfish >::Encryption blowfish_enc;
			blowfish_enc.SetKeyWithIV(&encryption_key[0], storage::basic_endpoint::encryption_key_t::static_size, iv, CryptoPP::Blowfish::BLOCKSIZE);

			std::size_t file_size = 0;
			{
				std::streambuf *pbuf = istr.rdbuf();
				file_size = pbuf->pubseekoff(0,std::ios::end, std::ios::in);
				pbuf->pubseekpos(0,std::ios::in);
			}

			if( 0 == file_size )
				throw rml::exception("Zero length input to encrypt()");

			//size_t padding_length = 8 - ( file_size % 8 );

			types::header_encrypted header;

			RML_DEBUG( assert(CryptoPP::Blowfish::BLOCKSIZE == sizeof(header.iv)) );
			memcpy(header.iv, iv, CryptoPP::Blowfish::BLOCKSIZE);

			ostr.write(reinterpret_cast<char*>(&header), sizeof(app::types::header_encrypted));

			CryptoPP::FileSource(istr,
					true,
					new CryptoPP::StreamTransformationFilter(
							blowfish_enc,
							new CryptoPP::FileSink(ostr),
							CryptoPP::BlockPaddingSchemeDef::PKCS_PADDING));
		}
		catch(const CryptoPP::Exception& e) {
			THROW_RML_EXCEPTION("CryptoPP::Exception: "<<e.what());
		}
	}

	void encrypt_aes(std::istream& istr, std::ostream& ostr, const storage::basic_endpoint::encryption_key_t& encryption_key)
	{
		try {
			using namespace archiving::archived::file_format;

			typedef crypto_header<version<2> > crypto_header_type;
			typedef crypto_header_type::encryption_algorithm_type::type encryption_algorithm;

			{
				std::streambuf *pbuf = istr.rdbuf();
				if (0 == pbuf->pubseekoff(0,std::ios::end, std::ios::in)) {
					throw rml::exception("Zero length input to encrypt()");
				}
				pbuf->pubseekpos(0,std::ios::in);
			}

			crypto_header_type crypto_header;
			initialise_crypto_header(crypto_header, version_number);
			rng_.GenerateBlock(crypto_header.iv, sizeof(crypto_header.iv));

			CryptoPP::CBC_Mode<encryption_algorithm>::Encryption encryption;
			encryption.SetKeyWithIV(&encryption_key[0], storage::basic_endpoint::encryption_key_t::static_size,
								   crypto_header.iv, sizeof(crypto_header.iv));

			ostr.write(reinterpret_cast<char*>(&crypto_header), sizeof(crypto_header_type));

			CryptoPP::FileSource(
					istr, true, new CryptoPP::StreamTransformationFilter(
							encryption, new CryptoPP::FileSink(ostr),
							CryptoPP::StreamTransformationFilter::PKCS_PADDING));
		}
		catch(const CryptoPP::Exception& e) {
			THROW_RML_EXCEPTION("encrypt_aes - CryptoPP::Exception: "<<e.what());
		}
	}

	inline void decrypt(std::istream& istr, std::ostream& ostr, operation_data::detail::pointer const& detail)
	{
		decrypt(istr, ostr, detail->p_endpoint->encryption_key);
	}

    void decrypt(std::istream& istr, std::ostream& ostr, const storage::basic_endpoint::encryption_key_t& encryption_key)
    {
        try {
            using namespace archiving::archived::file_format;

            istr.seekg(0); // make sure we are at offset 0

            crypto_header_union header;
            istr.read((char*)&header, sizeof(crypto_header_union));

            switch (header.format_version.major) {
            case 1: {
                typedef crypto_header<version<1> > crypto_header_type;
                typedef crypto_header_type::encryption_algorithm_type::type encryption_algorithm;

                if (!check_crypto_header_sanity(header.v1)) {
                    throw rml::exception("Invalid v1 header found");
                }

                CryptoPP::CBC_Mode<encryption_algorithm>::Decryption decryption;

                decryption.SetKeyWithIV(&encryption_key[0], encryption_key.size(), header.v1.iv, sizeof(header.v1.iv));
                CryptoPP::FileSource source(istr,
                                            true,
                                            new CryptoPP::StreamTransformationFilter(
                                                    decryption,
                                                    new CryptoPP::FileSink(ostr),
                                                    CryptoPP::StreamTransformationFilter::PKCS_PADDING));
                break;
            }
            case 2: {
                typedef crypto_header<version<2> > crypto_header_type;
                typedef crypto_header_type::encryption_algorithm_type::type encryption_algorithm;

                if (!check_crypto_header_sanity(header.v2)) {
                    throw rml::exception("Invalid v2 header found");
                }

                CryptoPP::CBC_Mode<encryption_algorithm>::Decryption decryption;

                decryption.SetKeyWithIV(&encryption_key[0], encryption_key.size(), header.v1.iv, sizeof(header.v1.iv));
                CryptoPP::FileSource source(istr,
                                            true,
                                            new CryptoPP::StreamTransformationFilter(
                                                    decryption,
                                                    new CryptoPP::FileSink(ostr),
                                                    CryptoPP::StreamTransformationFilter::PKCS_PADDING));
                break;
            }
            default:
                THROW_RML_EXCEPTION("Unknown header version v" << header.format_version.major);
            }
        }
        catch(const CryptoPP::Exception& e) {
            THROW_RML_EXCEPTION("CryptoPP::Exception: "<<e.what());
        }
    }

	void encrypt(operation_data::pointer const& opdata, const types::file_format::file_format_t& encrypted_format, const bool is_aes_enabled)
	{
		if(types::file_format::is_encrypted(opdata->p_detail->current_file_format))
			THROW_RML_EXCEPTION(opdata->p_detail->identifier << " already encrypted");

		encrypt(opdata->get_tmp_fstream(),
				opdata->get_tmp_fstream(encrypted_format, true, true),
				opdata->p_detail->current_file_format,
				opdata->p_detail->identifier,
				opdata->p_detail->p_endpoint->encryption_key,
				is_aes_enabled);

		opdata->get_tmp_fstream()->close();
		opdata->p_detail->current_file_format = encrypted_format;
	}

	inline void encrypt(
			rml::utils::tmp_fstream::pointer current_fstream,
			rml::utils::tmp_fstream::pointer encrypted_fstream,
			const types::file_format::file_format_t& current_file_format,
			const std::string& identifier,
			const storage::basic_endpoint::encryption_key_t& encryption_key,
			const bool is_aes_enabled)
	{
		if(types::file_format::is_encrypted(current_file_format))
			THROW_RML_EXCEPTION(identifier << " already encrypted");

		if(!encrypted_fstream)
			throw rml::exception("Invalid output stream");

		if(!types::file_format::is_compressed(current_file_format)) {
			LogMessage(identifier<<" Not compressed; adding precrypt header");

			// lets get a new temporary file inside the same location as the encrypted_fstream
			rml::utils::tmp_fstream::pointer p_tmp = rml::utils::tmp_fstream::create(encrypted_fstream->get_filename()+".pch_"/*XXXXXX*/, std::size_t(0), "/",
					std::fstream::in | std::fstream::out | std::fstream::binary | std::fstream::trunc);

			types::header_precrypt precrypt_hdr;
			precrypt_hdr.inflated_size = current_fstream->get_size();
			precrypt_hdr.content_length = precrypt_hdr.inflated_size;
			precrypt_hdr.format = types::data_format::clear;
			precrypt_hdr.sub_format = (current_file_format & 0xff);
			{
				CryptoPP::CRC32 hash;
				uint32_t crc32;
				CryptoPP::FileSource f(*current_fstream, true,
						new CryptoPP::HashFilter(hash,
								new CryptoPP::ArraySink((byte *)&crc32, CryptoPP::CRC32::DIGESTSIZE)));

				precrypt_hdr.crc32 = crc32;
			}

			current_fstream->clear();
			current_fstream->seekg(0);

			p_tmp->write((char*)&precrypt_hdr, sizeof(types::header_precrypt));
			*(p_tmp) << current_fstream->rdbuf();

			p_tmp->re_open();

			if (is_aes_enabled) {
				encrypt_aes(*p_tmp, *encrypted_fstream, encryption_key);
			} else {
				encrypt(*p_tmp, *encrypted_fstream, encryption_key);
			}
		}
		else {
			if (is_aes_enabled) {
				encrypt_aes(*current_fstream, *encrypted_fstream, encryption_key);
			} else {
				encrypt(*current_fstream, *encrypted_fstream, encryption_key);
			}
		}
	}

	void decrypt(operation_data::pointer const& opdata, const types::file_format::file_format_t& decrypted_format)
	{
		if(!types::file_format::is_encrypted(opdata->p_detail->current_file_format))
			THROW_RML_EXCEPTION(opdata->p_detail->identifier << " already decrypted");

		rml::utils::tmp_fstream::pointer current_fstream = opdata->get_tmp_fstream();

		rml::utils::tmp_fstream::pointer decrypted_fstream = opdata->get_tmp_fstream(decrypted_format, true, true);
		if(!decrypted_fstream)
			throw rml::exception("Unable to get tmp_fstream");

		decrypt(*current_fstream, *decrypted_fstream, opdata->p_detail);

		if(!types::file_format::is_compressed(decrypted_format)) {

			LogMessage(opdata->p_detail->identifier<<" Is not compressed; removing precrypt header");

			current_fstream = decrypted_fstream;

			decrypted_fstream = opdata->get_tmp_fstream(decrypted_format, true, true);
			if(!decrypted_fstream)
				throw rml::exception("Unable to get tmp_fstream");

			current_fstream->re_open();

			types::header_precrypt precrypt_hdr;
			current_fstream->read(reinterpret_cast<char *>(&precrypt_hdr), sizeof(types::header_precrypt));

			if(precrypt_hdr.format != types::data_format::clear)
				THROW_RML_EXCEPTION(opdata->p_detail->identifier<<" not 'clear' according to precrypt-header");

			if(precrypt_hdr.content_length != (current_fstream->get_size() - sizeof(types::header_precrypt)))
				THROW_RML_EXCEPTION(opdata->p_detail->identifier<<" file size mismatch: "
					<< precrypt_hdr.content_length << " vs " << current_fstream->get_size());

			// get_size() messes with the pointer
			current_fstream->seekg(sizeof(types::header_precrypt));

			*(decrypted_fstream) << current_fstream->rdbuf();

			decrypted_fstream->re_open();

			uint32_t crc32;
			{
				CryptoPP::CRC32 hash;
				CryptoPP::FileSource f(*decrypted_fstream, true,
						new CryptoPP::HashFilter(hash,
								new CryptoPP::ArraySink((byte *)&crc32, CryptoPP::CRC32::DIGESTSIZE)));
			}

			if(crc32 != precrypt_hdr.crc32)
				THROW_RML_EXCEPTION(opdata->p_detail->identifier<<" CRC mismatch: "
					<< crc32 << " vs " << precrypt_hdr.crc32);
		}

		opdata->get_tmp_fstream()->close();
		opdata->p_detail->current_file_format = decrypted_format;
	}
};

}  // namespace app

#endif /* CRYPTO_HPP_ */
