
// Copyright (c) 2014 Google, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

/*
* This file is a modified version of
* https://github.com/google/farmhash/blob/0d859a811870d10f53a594927d0d0b97573ad06d/dev/farmhashna.cc
*/

#include "hash/HashLib.h"
#include <cstdint>
#include <cassert>
#include <cstring>

#define k0 0xC3A5C85C97CB3127L
#define k1 0xB492B66FBE98F273L
#define k2 0x9AE16A3B2F90404FL

//#undef Fetch
//#define Fetch Fetch64
uint64_t Fetch(const char* p) {
    uint64_t result;
    memcpy(&result, p, sizeof(result));
    return result; // Endianness check but it should be fine since we can assume little endian
}

uint32_t Fetch32(const char* p) {
    uint32_t result;
    memcpy(&result, p, sizeof(result));
    return result;
}

void swapperino(uint64_t& a, uint64_t& b) {
    uint64_t temp = a;
    a = b;
    b = temp;
}

//#undef Rotate
//#define Rotate Rotate64
uint64_t Rotate(uint64_t val, int shift) {
    //return sizeof(unsigned long) == sizeof(val) ?
    //    _lrotr(val, shift) :
    //    BasicRotate64(val, shift);

    return shift == 0 ? val : ((val >> shift) | (val << (64 - shift)));
}

struct Pair64 {
    uint64_t first;
    uint64_t second;
};

uint64_t ShiftMix(uint64_t val) {
    return val ^ (val >> 47);
}

//uint64_t HashLen16(uint64_t u, uint64_t v) {
//    return Hash128to64(Uint128(u, v));
//}

uint64_t HashLen16(uint64_t u, uint64_t v, uint64_t mul) {
    // Murmur-inspired hashing.
    uint64_t a = (u ^ v) * mul;
    a ^= (a >> 47);
    uint64_t b = (v ^ a) * mul;
    b ^= (b >> 47);
    b *= mul;
    return b;
}

uint64_t HashLen0to16(const char* s, size_t len) {
    if (len >= 8) {
        uint64_t mul = k2 + len * 2;
        uint64_t a = Fetch(s) + k2;
        uint64_t b = Fetch(s + len - 8);
        uint64_t c = Rotate(b, 37) * mul + a;
        uint64_t d = (Rotate(a, 25) + b) * mul;
        return HashLen16(c, d, mul);
    }
    if (len >= 4) {
        uint64_t mul = k2 + len * 2;
        uint64_t a = Fetch32(s);
        return HashLen16(len + (a << 3), Fetch32(s + len - 4), mul);
    }
    if (len > 0) {
        uint8_t a = s[0];
        uint8_t b = s[len >> 1];
        uint8_t c = s[len - 1];
        uint32_t y = static_cast<uint32_t>(a) + (static_cast<uint32_t>(b) << 8);
        uint32_t z = len + (static_cast<uint32_t>(c) << 2);
        return ShiftMix(y * k2 ^ z * k0) * k2;
    }
    return k2;
}

// This probably works well for 16-byte strings as well, but it may be overkill
// in that case.
inline uint64_t HashLen17to32(const char* s, size_t len) {
    uint64_t mul = k2 + len * 2;
    uint64_t a = Fetch(s) * k1;
    uint64_t b = Fetch(s + 8);
    uint64_t c = Fetch(s + len - 8) * mul;
    uint64_t d = Fetch(s + len - 16) * k2;
    return HashLen16(Rotate(a + b, 43) + Rotate(c, 30) + d,
        a + Rotate(b + k2, 18) + c, mul);
}

// Return a 16-byte hash for 48 bytes.  Quick and dirty.
// Callers do best to use "random-looking" values for a and b.
inline Pair64 WeakHashLen32WithSeeds(
    uint64_t w, uint64_t x, uint64_t y, uint64_t z, uint64_t a, uint64_t b) {
    a += w;
    b = Rotate(b + a + z, 21);
    uint64_t c = a;
    a += x;
    a += y;
    b += Rotate(a, 44);
    return {a + z, b + c};
}

// Return a 16-byte hash for s[0] ... s[31], a, and b.  Quick and dirty.
inline Pair64 WeakHashLen32WithSeeds(
    const char* s, uint64_t a, uint64_t b) {
    return WeakHashLen32WithSeeds(Fetch(s),
        Fetch(s + 8),
        Fetch(s + 16),
        Fetch(s + 24),
        a,
        b);
}

// Return an 8-byte hash for 33 to 64 bytes.
inline uint64_t HashLen33to64(const char* s, size_t len) {
    uint64_t mul = k2 + len * 2;
    uint64_t a = Fetch(s) * k2;
    uint64_t b = Fetch(s + 8);
    uint64_t c = Fetch(s + len - 8) * mul;
    uint64_t d = Fetch(s + len - 16) * k2;
    uint64_t y = Rotate(a + b, 43) + Rotate(c, 30) + d;
    uint64_t z = HashLen16(y, a + Rotate(b + k2, 18) + c, mul);
    uint64_t e = Fetch(s + 16) * mul;
    uint64_t f = Fetch(s + 24);
    uint64_t g = (y + Fetch(s + len - 32)) * mul;
    uint64_t h = (z + Fetch(s + len - 24)) * mul;
    return HashLen16(Rotate(e + f, 43) + Rotate(g, 30) + h,
        e + Rotate(f + a, 18) + g, mul);
}

uint64_t HashLib::FarmHash64(const char* s, size_t len) {
    const uint64_t seed = 81;
    if (len <= 32) {
        if (len <= 16) {
            return HashLen0to16(s, len);
        }
        else {
            return HashLen17to32(s, len);
        }
    }
    else if (len <= 64) {
        return HashLen33to64(s, len);
    }

    // For strings over 64 bytes we loop.  Internal state consists of
    // 56 bytes: v, w, x, y, and z.
    uint64_t x = seed;
    uint64_t y = seed * k1 + 113;
    uint64_t z = ShiftMix(y * k2 + 113) * k2;
    Pair64 v = {0, 0};
    Pair64 w = {0, 0};
    x = x * k2 + Fetch(s);

    // Set end so that after the loop we have 1 to 64 bytes left to process.
    const char* end = s + ((len - 1) / 64) * 64;
    const char* last64 = end + ((len - 1) & 63) - 63;
    assert(s + len - 64 == last64);
    do {
        x = Rotate(x + y + v.first + Fetch(s + 8), 37) * k1;
        y = Rotate(y + v.second + Fetch(s + 48), 42) * k1;
        x ^= w.second;
        y += v.first + Fetch(s + 40);
        z = Rotate(z + w.first, 33) * k1;
        v = WeakHashLen32WithSeeds(s, v.second * k1, x + w.first);
        w = WeakHashLen32WithSeeds(s + 32, z + w.second, y + Fetch(s + 16));
        swapperino(z, x);
        s += 64;
    } while (s != end);
    uint64_t mul = k1 + ((z & 0xff) << 1);
    // Make s point to the last 64 bytes of input.
    s = last64;
    w.first += ((len - 1) & 63);
    v.first += w.first;
    w.first += v.first;
    x = Rotate(x + y + v.first + Fetch(s + 8), 37) * mul;
    y = Rotate(y + v.second + Fetch(s + 48), 42) * mul;
    x ^= w.second * 9;
    y += v.first * 9 + Fetch(s + 40);
    z = Rotate(z + w.first, 33) * mul;
    v = WeakHashLen32WithSeeds(s, v.second * mul, x + w.first);
    w = WeakHashLen32WithSeeds(s + 32, z + w.second, y + Fetch(s + 16));
    swapperino(z, x);
    return HashLen16(HashLen16(v.first, w.first, mul) + ShiftMix(y) * k0 + z,
        HashLen16(v.second, w.second, mul) + x,
        mul);
}

//uint64_t Hash64WithSeeds(const char* s, size_t len, uint64_t seed0, uint64_t seed1);
//
//uint64_t Hash64WithSeed(const char* s, size_t len, uint64_t seed) {
//    return Hash64WithSeeds(s, len, k2, seed);
//}
//
//uint64_t Hash64WithSeeds(const char* s, size_t len, uint64_t seed0, uint64_t seed1) {
//    return HashLen16(Hash64(s, len) - seed0, seed1);
//}