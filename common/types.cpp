/*
 * types.hpp
 *
 *  Created on: 25 Jan 2011
 *      Author: dkorah
 */

#include "types.hpp"

namespace std {

std::ostream& operator<<(std::ostream& ostr, const archiving::hash_vector& hash)
{
	string ret;

	CryptoPP::HexEncoder encoder(0, false);
	encoder.Put(&hash[0], hash.size());

	size_t size = encoder.MaxRetrievable();
	if(size) {
	    ret.resize(size);
	    encoder.Get((byte*)&ret[0], ret.size());
	}

	return ostr << ret;
}

std::istream& operator>>(std::istream& istr, archiving::hash_vector& hv)
{
	string encoded;
	istr >> encoded;

	assert(archiving::hash_vector::static_size*2 == encoded.length());

	CryptoPP::HexDecoder decoder;

	decoder.Put( (byte*)&encoded[0], encoded.length() );
	decoder.MessageEnd();

	size_t size = decoder.MaxRetrievable();
	if(size)
	    decoder.Get(&hv[0], archiving::hash_vector::static_size);

	return istr;
}

}
