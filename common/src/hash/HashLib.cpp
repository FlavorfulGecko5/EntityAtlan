#include "HashLib.h"

uint64_t HashLib::FingerPrint(uint64_t hi, uint64_t lo) {
	uint64_t kMul = 0x9DDFEA08EB382D69L;
	uint64_t a = (lo ^ hi) * kMul;
	a ^= a >> 47;

	uint64_t b = (hi ^ a) * kMul;
	b ^= b >> 44;
	b *= kMul;
	b ^= b >> 41;
	b *= kMul;
	return b;
}

uint64_t HashLib::DeclHash(std::string_view type, std::string_view name) {
	uint64_t lo = FarmHash64(type.data(), type.length());
	uint64_t hi = FarmHash64(name.data(), name.length());
	uint64_t v10 = FingerPrint(hi, lo);

	return v10;
}

/*
* Credits: MurmurHash implementation was taken from 
* https://github.com/explosion/murmurhash/blob/master/murmurhash/MurmurHash2.cpp#L176
*/


static inline uint32_t getblock(const uint32_t* p)
{
	#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
	return *p;
	#else
	const uint8_t* c = (const uint8_t*)p;
	return (uint32_t)c[0] |
		(uint32_t)c[1] << 8 |
		(uint32_t)c[2] << 16 |
		(uint32_t)c[3] << 24;
	#endif
}

static inline uint64_t getblock(const uint64_t* p)
{
	#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
	return *p;
	#else
	const uint8_t* c = (const uint8_t*)p;
	return (uint64_t)c[0] |
		(uint64_t)c[1] << 8 |
		(uint64_t)c[2] << 16 |
		(uint64_t)c[3] << 24 |
		(uint64_t)c[4] << 32 |
		(uint64_t)c[5] << 40 |
		(uint64_t)c[6] << 48 |
		(uint64_t)c[7] << 56;
	#endif
}

uint64_t MurmurHash64B(const void* key, int len, uint64_t seed)
{
	const uint32_t m = 0x5bd1e995;
	const int r = 24;

	uint32_t h1 = uint32_t(seed) ^ len;
	uint32_t h2 = uint32_t(seed >> 32);

	const uint32_t* data = (const uint32_t*)key;

	while (len >= 8)
	{
		uint32_t k1 = getblock(data++);
		k1 *= m; k1 ^= k1 >> r; k1 *= m;
		h1 *= m; h1 ^= k1;
		len -= 4;

		uint32_t k2 = getblock(data++);
		k2 *= m; k2 ^= k2 >> r; k2 *= m;
		h2 *= m; h2 ^= k2;
		len -= 4;
	}

	if (len >= 4)
	{
		uint32_t k1 = getblock(data++);
		k1 *= m; k1 ^= k1 >> r; k1 *= m;
		h1 *= m; h1 ^= k1;
		len -= 4;
	}

	switch (len)
	{
		case 3: h2 ^= ((unsigned char*)data)[2] << 16;
		case 2: h2 ^= ((unsigned char*)data)[1] << 8;
		case 1: h2 ^= ((unsigned char*)data)[0];
		h2 *= m;
	};

	h1 ^= h2 >> 18; h1 *= m;
	h2 ^= h1 >> 22; h2 *= m;
	h1 ^= h2 >> 17; h1 *= m;
	h2 ^= h1 >> 19; h2 *= m;

	uint64_t h = h1;

	h = (h << 32) | h2;

	return h;
}

uint64_t HashLib::ResourceMurmurHash(const char* data, size_t length) {
	return MurmurHash64B(data, length, 0xDEADBEEFUL);
}


uint32_t HashLib::idHashIndex(const char* string, size_t length) {
	int hash = 0;
	const char* max = string + length;

	while (string < max) {
		hash = (hash << 5) - hash + (unsigned char)(*string);
		string++;
	}
	return *reinterpret_cast<uint32_t*>(&hash);
}

uint32_t HashLib::akfnv_insensitive(const char* in_pData, size_t in_dataSize)
{
	const uint32_t HASHPRIME = 16777619;
	const uint32_t OFFSET = 2166136261U;

	const unsigned char* pData = (const unsigned char*)in_pData;
	const unsigned char* pEnd = pData + in_dataSize;        /* beyond end of buffer */

	uint32_t hval = OFFSET;
	while (pData < pEnd)
	{
		hval *= HASHPRIME;

		unsigned char c = (unsigned char)*pData++;
		c = (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
		hval ^= c;
	}

	return hval;
}

uint64_t mix33(uint64_t num) {
	return num ^ (num >> 33);
}

uint64_t HashLib::streamdb_miphash(uint64_t defaultHash, uint64_t mipId, uint64_t zero)
{
	const uint64_t C1 = 0xFF51AFD7ED558CCDULL;
	const uint64_t C2 = 0xC4CEB9FE1A85EC53ULL;
	const uint64_t C3 = 0x000000009E3779B9ULL;

	uint64_t k0 = *reinterpret_cast<int64_t*>(&defaultHash);
	uint64_t k1 = C1 * mipId;
	uint64_t k2 = C1 * zero;

	uint64_t p1 = mix33(k2);
	uint64_t p2 = ((C2 * mix33(k1)) >> 33) ^ (C2 * mix33(k1));
	uint64_t finalHash = k0 ^ ((k0 >> 2) + (k0 << 6) + (p2 ^ ((p2 >> 2) + (p2 << 6) + (mix33(C2 * p1)) + C3)) + C3);
	return finalHash;
}
