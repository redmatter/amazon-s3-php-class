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
#include <cryptopp/files.h>
#include <cryptopp/crc.h>


#include <rml/utils.h>

#include "types.hpp"
#include "operation_data.hpp"

namespace app {

namespace impl {

class crypto
{
	CryptoPP::AutoSeededRandomPool rng_;

public:
	inline void encrypt(std::istream& istr, std::ostream& ostr, operation_data::detail::pointer const& detail)
	{
		encrypt(istr, ostr, detail->p_endpoint->encryption_key);
	}

	void encrypt(std::istream& istr, std::ostream& ostr, const std::vector<uint8_t>& encryption_key)
	{
		try {
			uint8_t iv[CryptoPP::Blowfish::BLOCKSIZE];
			rng_.GenerateBlock(iv, sizeof(iv));

			CryptoPP::CBC_Mode< CryptoPP::Blowfish >::Encryption blowfish_enc;
			blowfish_enc.SetKeyWithIV(&encryption_key[0], encryption_key.size(), iv, CryptoPP::Blowfish::BLOCKSIZE);

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

#if DEBUG >= 1
			assert(CryptoPP::Blowfish::BLOCKSIZE == sizeof(header.iv));
#endif
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

	inline void decrypt(std::istream& istr, std::ostream& ostr, operation_data::detail::pointer const& detail)
	{
		decrypt(istr, ostr, detail->p_endpoint->encryption_key);
	}

	void decrypt(std::istream& istr, std::ostream& ostr, const std::vector<uint8_t>& encryption_key)
	{
		try {
			CryptoPP::CBC_Mode< CryptoPP::Blowfish >::Decryption blowfish_dec;

			istr.seekg(0); // make sure we are at offset 0

			types::header_encrypted header;
			types::header_encrypted blank_header;
#if DEBUG >= 1
			assert(CryptoPP::Blowfish::BLOCKSIZE == sizeof(header.iv));
#endif
			istr.read((char*)&header, sizeof(types::header_encrypted));
			if(0 != memcmp(blank_header.magic, header.magic, sizeof(blank_header.magic)))
				throw rml::exception("header found corrupt; magic pattern did not match");

			// TODO change this to handle differences in future header changes
			if(0 != memcmp(blank_header.padding, header.padding, sizeof(blank_header.padding)))
				throw rml::exception("header found corrupt; padding pattern did not match");

			// TODO change this to handle future version differences, rather than checking for equality
			if((header.format_version_major != app::types::file_format_version::major) || (header.format_version_minor != app::types::file_format_version::minor))
				throw rml::exception("header found corrupt; Invalid file format version");

			blowfish_dec.SetKeyWithIV(&encryption_key[0], encryption_key.size(), header.iv, CryptoPP::Blowfish::BLOCKSIZE);
			CryptoPP::FileSource source(istr,
					true,
					new CryptoPP::StreamTransformationFilter(
							blowfish_dec,
							new CryptoPP::FileSink(ostr),
							CryptoPP::BlockPaddingSchemeDef::PKCS_PADDING));
		}
		catch(const CryptoPP::Exception& e) {
			THROW_RML_EXCEPTION("CryptoPP::Exception: "<<e.what());
		}
	}

	void encrypt(operation_data::pointer const& opdata, const types::file_format::file_format_t& encrypted_format)
	{
		if(types::file_format::is_encrypted(opdata->p_detail->current_file_format))
			THROW_RML_EXCEPTION(opdata->p_detail->identifier << " already encrypted");

		rml::utils::tmp_fstream::pointer current_fstream = opdata->get_tmp_fstream();

		rml::utils::tmp_fstream::pointer encrypted_fstream = opdata->get_tmp_fstream(encrypted_format, true, true);
		if(!encrypted_fstream)
			throw rml::exception("Unable to get tmp_fstream");

		if(!types::file_format::is_compressed(opdata->p_detail->current_file_format)) {
			LogMessage(opdata->p_detail->identifier<<" Not compressed; adding precrypt header");

			types::header_precrypt precrypt_hdr;
			precrypt_hdr.inflated_size = current_fstream->get_size();
			precrypt_hdr.content_length = precrypt_hdr.inflated_size;
			precrypt_hdr.format = types::data_format::clear;
			precrypt_hdr.sub_format = (opdata->p_detail->current_file_format & 0xff);
			{
				CryptoPP::CRC32 hash;
				uint32_t crc32;
				CryptoPP::FileSource f(*(current_fstream->get_istream_ptr()), true,
						new CryptoPP::HashFilter(hash,
								new CryptoPP::ArraySink((byte *)&crc32, CryptoPP::CRC32::DIGESTSIZE)));

				precrypt_hdr.crc32 = crc32;
			}

			rml::utils::tmp_fstream::pointer bare_stream = current_fstream;

			// here we are using encrypted_fstream only as a temporary file under the same storage identifier

			encrypted_fstream->write(reinterpret_cast<char*>(&precrypt_hdr), sizeof(types::header_precrypt));
			*(encrypted_fstream) << current_fstream->rdbuf();

			// set the new tmp file as current
			current_fstream = encrypted_fstream;
			current_fstream->seekg(0);

			// now we need to create a new tmp fstream for the encrypted file
			encrypted_fstream = opdata->get_tmp_fstream(encrypted_format, true, true);
			if(!encrypted_fstream)
				throw rml::exception("Unable to get tmp_fstream");
		}

		encrypt(*(current_fstream->get_istream_ptr()), *(encrypted_fstream->get_ostream_ptr()), opdata->p_detail);

		opdata->p_detail->current_file_format = encrypted_format;
	}

	void decrypt(operation_data::pointer const& opdata, const types::file_format::file_format_t& decrypted_format)
	{
		if(!types::file_format::is_encrypted(opdata->p_detail->current_file_format))
			THROW_RML_EXCEPTION(opdata->p_detail->identifier << " already decrypted");

		rml::utils::tmp_fstream::pointer current_fstream = opdata->get_tmp_fstream();

		rml::utils::tmp_fstream::pointer decrypted_fstream = opdata->get_tmp_fstream(decrypted_format, true, true);
		if(!decrypted_fstream)
			throw rml::exception("Unable to get tmp_fstream");

		decrypt(*(current_fstream->get_istream_ptr()), *(decrypted_fstream->get_ostream_ptr()), opdata->p_detail);

		if(!types::file_format::is_compressed(decrypted_format)) {

			LogMessage(opdata->p_detail->identifier<<" Is not compressed; removing precrypt header");

			current_fstream = decrypted_fstream;

			rml::utils::tmp_fstream::pointer decrypted_fstream = opdata->get_tmp_fstream(decrypted_format, true, true);
			if(!decrypted_fstream)
				throw rml::exception("Unable to get tmp_fstream");

			current_fstream->seekg(0);

			types::header_precrypt precrypt_hdr;
			current_fstream->read(reinterpret_cast<char *>(&precrypt_hdr), sizeof(types::header_precrypt));

			if(precrypt_hdr.format != types::data_format::clear)
				THROW_RML_EXCEPTION(opdata->p_detail->identifier<<" not 'clear' according to precrypt-header");

			if(precrypt_hdr.content_length != (current_fstream->get_size() - sizeof(types::header_precrypt)))
				THROW_RML_EXCEPTION(opdata->p_detail->identifier<<" file size mismatch");

			*(decrypted_fstream) << current_fstream->rdbuf();

			decrypted_fstream->seekg(0);

			uint32_t crc32;
			{
				CryptoPP::CRC32 hash;
				CryptoPP::FileSource f(*(decrypted_fstream->get_istream_ptr()), true,
						new CryptoPP::HashFilter(hash,
								new CryptoPP::ArraySink((byte *)&crc32, CryptoPP::CRC32::DIGESTSIZE)));
			}

			if(crc32 != precrypt_hdr.crc32)
				THROW_RML_EXCEPTION(opdata->p_detail->identifier<<" CRC mismatch");
		}

		opdata->p_detail->current_file_format = decrypted_format;
	}
};

}  // namespace impl

class crypto : private impl::crypto
{
	app::operation_data::channel_t& crypto_channel_;
	app::operation_data::channel_t& main_channel_;
	volatile bool& quit_flag_;
public:
	crypto(app::operation_data::channel_t& crypto_channel, app::operation_data::channel_t& main_channel, volatile bool& quit_flag) :
		crypto_channel_(crypto_channel), main_channel_(main_channel), quit_flag_(quit_flag)
	{
	}

	void run()
	{
		while(!quit_flag_) {
			try {
				operation_data::pointer opdata = crypto_channel_.consume();

				if(opdata->state != operation_states::get_file_convert || opdata->state != operation_states::put_file_convert);
					THROW_RML_EXCEPTION(opdata->p_detail->identifier << " Invalid call sequence");

				const std::pair<types::file_format::convert_action::convert_action_t, types::file_format::file_format_t>& next(
						types::file_format::get_next(opdata->p_detail->current_file_format, opdata->p_detail->target_file_format));

				switch(next.first) {
				case types::file_format::convert_action::encrypt:
					encrypt(opdata, next.second);
					break;

				case types::file_format::convert_action::decrypt:
					decrypt(opdata, next.second);
					break;

				default:
					THROW_RML_EXCEPTION(opdata->p_detail->identifier << " Invalid call sequence");
				}

				{
					const std::pair<types::file_format::convert_action::convert_action_t, types::file_format::file_format_t>& next(
							types::file_format::get_next(opdata->p_detail->current_file_format, opdata->p_detail->target_file_format));

					if(next.first == types::file_format::convert_action::nothing) // we dont care if the format is good; at least no infinite loop here!
						opdata->state = operation_states::operation_states_t(opdata->state + 1); // let it move on from "convert"
				}

				// put it back to the main channel
				main_channel_.produce(opdata);
			}
			catch(const app::operation_data::channel_t::finishing_exception& e) {
				LogMessage("Channel(s) finishing");
				break;
			}
			catch(const std::exception& e) {
				LogError("exception: "<<e.what());
			}
			catch(...) {
				LogError("Unexpected exception");
			}
		}
	}
};

namespace utils {

class crypto : public impl::crypto
{
};

} // namespace utils

}  // namespace app

#endif /* CRYPTO_HPP_ */
