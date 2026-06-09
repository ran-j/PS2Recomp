#pragma once

#include <cstdint>

using usz = size_t;
using uint = unsigned int;
using sint = signed int;

using u64 = uint64_t;
using u32 = uint32_t;
using u16 = uint16_t;
using u8 = uint8_t;

constexpr usz operator ""_kb(usz bytes)
{
	return bytes * 1024;
}

constexpr usz operator ""_mb(usz bytes)
{
	return bytes * 1024 * 1024;
}