#pragma once

#include <cstdint>

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(USE_SSE2NEON)
#include "sse2neon.h"
#else
#include <immintrin.h> // For SSE/AVX instructions
#include <smmintrin.h> // For SSE4.1 instructions
#endif

using usz = size_t;
using uint = unsigned int;
using sint = signed int;
using ull = unsigned long long;

using u64 = uint64_t;
using u32 = uint32_t;
using u16 = uint16_t;
using u8 = uint8_t;

using s128 = __m128i;
using f128 = __m128;

constexpr usz operator ""_kb(ull bytes)
{
	return bytes * 1024;
}

constexpr usz operator ""_mb(ull bytes)
{
	return bytes * 1024 * 1024;
}