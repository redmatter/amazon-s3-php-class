/*
 * crypto.hpp
 *
 *  Created on: 2 Feb 2011
 *      Author: dkorah
 */

#ifndef CRYPTO_HPP_
#define CRYPTO_HPP_

#include <string.h>

#include <cryptopp/osrng.h>
#include <cryptopp/cryptlib.h>
#include <cryptopp/hex.h>
#include <cryptopp/filters.h>
#include <cryptopp/blowfish.h>
#include <cryptopp/modes.h>
#include <cryptopp/secblock.h>

#include <rml/utils.h>

#include "types.hpp"

namespace app {

class crypto {
	CryptoPP::AutoSeededRandomPool rng_;

public:
	crypto() {

	}

	void encrypt(rml::utils::tmp_fstream& f, detail d, const std::string& out_dir, const std::string& identifier) {
		try {
			uint8_t iv[CryptoPP::Blowfish::BLOCKSIZE];
			rng_.GenerateBlock(iv, sizeof(iv));

			CryptoPP::CBC_Mode< CryptoPP::Blowfish >::Encryption blowfish_enc;
			blowfish_enc.SetKeyWithIV(d.encryption_key, d.encryption_key.size(), iv, CryptoPP::Blowfish::BLOCKSIZE);

			std::istream& f_in(*(f.get_istream_ptr()));
			std::size_t file_size = 0;
			{
				std::filebuf *pbuf = f_in.rdbuf();
				file_size = pbuf->pubseekoff(0,std::ios::end, std::ios::in);
				pbuf->pubseekpos(0,std::ios::in);
			}

			if( 0 == file_size )
				throw rml::exception("Zero length input to encrypt()");

			types::header_precrypt& header_precrypt( d.header );

			size_t padding_length = 8 - ( file_size % 8 );

			rml::utils::tmp_fstream::pointer tmp_file(
					new rml::utils::tmp_fstream(
							f.get_filename(true),
							file_size + sizeof(types::header_encrypted) + sizeof(types::header_precrypt) + padding_length,
							f.get_directory()));

			types::header_encrypted header;

#if DEBUG >= 1
			assert(CryptoPP::Blowfish::BLOCKSIZE == sizeof(header.iv));
#endif
			memcpy(header.iv, iv, CryptoPP::Blowfish::BLOCKSIZE);

			tmp_file->write(reinterpret_cast<char*>(&header), sizeof(app::types::header_encrypted));

			std::ofstream& f_out(*(tmp_file->get_ostream_ptr()));

			CryptoPP::StringSource source(reinterpret_cast<char*>(&header_precrypt), sizeof(types::header_precrypt),
					true,
					new CryptoPP::StreamTransformationFilter(
							blowfish_enc,
							new CryptoPP::FileSink(f_out),
							CryptoPP::NO_PADDING));

			CryptoPP::FileSource source(f_in,
					true,
					new CryptoPP::StreamTransformationFilter(
							blowfish_enc,
							new CryptoPP::FileSink(f_out),
							CryptoPP::NO_PADDING));

			uint8_t padding[padding_length];
			memset(padding, (int)padding_length, padding_length);

			CryptoPP::StringSource source(padding, padding_length,
					true,
					new CryptoPP::StreamTransformationFilter(
							blowfish_enc,
							new CryptoPP::FileSink(f_out),
							CryptoPP::NO_PADDING));
		}
		catch(const CryptoPP::Exception& e) {
			THROW_RML_EXCEPTION("CryptoPP::Exception: "<<e.what());
		}
	}

	void decrypt(rml::utils::tmp_fstream& f, detail d, const std::string& out_dir, const std::string& identifier) {
		try {
			uint8_t iv[CryptoPP::Blowfish::BLOCKSIZE];

			CryptoPP::CBC_Mode< CryptoPP::Blowfish >::Decryption blowfish_dec;

			std::istream& f_in(*(f.get_istream_ptr()));
			f_in.rdbuf()->pubseekpos(0,std::ios::in); // make sure we are at offset 0

			types::header_encrypted header;
#if DEBUG >= 1
			assert(CryptoPP::Blowfish::BLOCKSIZE == sizeof(header.iv));
#endif
			f_in.read(reinterpret_cast<char*>(&header), sizeof(types::header_encrypted));

			if(0 != memcmp("RMA-CE", header.magic, 6))
				throw rml::exception("header found corrupt; magic pattern did not match");

			// TODO change this to handle differences in future header changes
			if(0 != memcmp("RedMatterRedMatterRedMatterRedMatterRedMatterRedMatterRedMatter", header.padding, sizeof(header.padding)))
				throw rml::exception("header found corrupt; padding pattern did not match");

			// TODO change this to handle future version differences, rather than checking for equality
			if((header.format_version_major != app::types::file_format_version::major) || (header.format_version_minor != app::types::file_format_version::minor))
				throw rml::exception("header found corrupt; Invalid file format version");

			blowfish_dec.SetKeyWithIV(d.encryption_key, d.encryption_key.size(), header.iv, CryptoPP::Blowfish::BLOCKSIZE);

			types::header_precrypt header_precrypt;
			std::string header_precrypt_str(sizeof(types::header_precrypt));

			CryptoPP::FileSource source(f_in,
					true,
					new CryptoPP::StreamTransformationFilter(
							blowfish_dec,
							new StringSink(header_precrypt_str),
							CryptoPP::NO_PADDING));

			memcpy(reinterpret_cast<char*>(&header_precrypt), &header_precrypt_str[0], sizeof(types::header_precrypt));

			rml::utils::tmp_fstream::pointer tmp_file;
			{
				std::ostringstream ostr;
				ostr << "dec_" << identifier;
				tmp_file.reset(
						new rml::utils::tmp_fstream(
								ostr.str(),
								header_precrypt.content_length,
								f.get_directory()));
			}

			std::ofstream& f_out(*(tmp_file->get_ostream_ptr()));

			CryptoPP::FileSource source(f_in,
					false,
					new CryptoPP::StreamTransformationFilter(
							blowfish_dec,
							new CryptoPP::FileSink(f_out),
							CryptoPP::NO_PADDING));

			{
				size_t pumped = source.Pump(header_precrypt.content_length);
				assert(pumped == header_precrypt.content_length);
			}


			size_t padding_length = 8 - ( header_precrypt.content_length % 8 );
			std::string padding(padding_length);
			CryptoPP::FileSource source(f_in,
					true,
					new CryptoPP::StreamTransformationFilter(
							blowfish_dec,
							new StringSink(padding),
							CryptoPP::NO_PADDING));

			if(padding != std::string(padding_length, (char)padding_length))
				throw rml::exception("Invalid padding");

			CryptoPP::FileSource source(f_in,
					true,
					new CryptoPP::StreamTransformationFilter(
							blowfish_enc,
							new CryptoPP::FileSink(f_out),
							CryptoPP::NO_PADDING));

			// TODO return
		}
		catch(const CryptoPP::Exception& e) {
			THROW_RML_EXCEPTION("CryptoPP::Exception: "<<e.what());
		}
	}

	void run() {
	}
};

}  // namespace app

#endif /* CRYPTO_HPP_ */
