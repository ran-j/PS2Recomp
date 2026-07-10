#pragma once

#include <cstdint>
#include <type_traits>
#include <concepts>
#include <bit>

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

using s64 = int64_t;
using s32 = int32_t;
using s16 = int16_t;
using s8 = int8_t;

using f32 = float;
using f64 = double;

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

// C++23 std::to_underlying()
template<typename T>
constexpr auto ToUnderlying(T v) -> std::underlying_type_t<T>
{
	return static_cast<std::underlying_type_t<T>>(v);
}

template<typename T>
concept IsSinglePrecisionFloat = std::floating_point<T> && sizeof(T) == 4;

template<typename T, usz Offset, usz BitCount, typename U = T>
struct Bitfield
{
	using BackingType = T;
	using ValueType = U;

	static constexpr usz MaxWidth = sizeof(BackingType) * 8;
	static constexpr usz One = ~static_cast<BackingType>(0);
	static constexpr usz Shift = MaxWidth - BitCount;
	static constexpr usz WidthMask = One >> Shift;
	static constexpr usz ValueMask = WidthMask << Offset;

	static_assert(BitCount > 0 && Offset + BitCount <= MaxWidth);
	static_assert(!(IsSinglePrecisionFloat<T> && BitCount != 32));

	inline constexpr auto Raw() const -> BackingType;

	inline constexpr auto Value() const -> ValueType
		requires std::unsigned_integral<ValueType>;

	inline constexpr auto Value() const -> ValueType
		requires std::signed_integral<ValueType>;

	inline constexpr auto Value() const -> ValueType
		requires IsSinglePrecisionFloat<ValueType>;

	inline constexpr auto Store(ValueType val) -> void
		requires std::integral<ValueType>;

	inline constexpr auto Store(ValueType val) -> void
		requires IsSinglePrecisionFloat<ValueType>;

	inline constexpr operator ValueType() const;
	inline constexpr Bitfield& operator=(ValueType val);

	BackingType data{ };
};

template<typename T, usz Offset, usz BitCount, typename ValueType>
constexpr auto Bitfield<T, Offset, BitCount, ValueType>::Raw() const -> BackingType
{
	return (data & ValueMask) >> Offset;
}

template<typename T, usz Offset, usz BitCount, typename ValueType>
constexpr auto Bitfield<T, Offset, BitCount, ValueType>::Value() const -> ValueType
	requires std::unsigned_integral<ValueType>
{
	return static_cast<ValueType>(Raw());
}

template<typename T, usz Offset, usz BitCount, typename ValueType>
constexpr auto Bitfield<T, Offset, BitCount, ValueType>::Value() const -> ValueType
	requires std::signed_integral<ValueType>
{
	using SignedType = std::make_signed_t<BackingType>;
	const auto bits = static_cast<SignedType>(Raw() << Shift); // Shift sign to MSB

	return static_cast<ValueType>(bits >> Shift); // Shift right keeping sign
}

template<typename T, usz Offset, usz BitCount, typename ValueType>
constexpr auto Bitfield<T, Offset, BitCount, ValueType>::Value() const -> ValueType
	requires IsSinglePrecisionFloat<ValueType>
{
	return std::bit_cast<ValueType>(static_cast<u32>(Raw()));
}

template<typename T, usz Offset, usz BitCount, typename ValueType>
constexpr auto Bitfield<T, Offset, BitCount, ValueType>::Store(ValueType val) -> void
	requires std::integral<ValueType>
{
	const auto bits = static_cast<BackingType>(val);

	data = (data & ~ValueMask) | ((bits << Offset) & ValueMask);
}

template<typename T, usz Offset, usz BitCount, typename ValueType>
constexpr auto Bitfield<T, Offset, BitCount, ValueType>::Store(ValueType val) -> void
	requires IsSinglePrecisionFloat<ValueType>
{
	const auto bits = static_cast<BackingType>( std::bit_cast<u32>(val) );

	data = (data & ~ValueMask) | ((bits << Offset) & ValueMask);
}

template<typename T, usz Offset, usz BitCount, typename ValueType>
constexpr Bitfield<T, Offset, BitCount, ValueType>:: operator ValueType() const
{
	return Value();
}

template<typename T, usz Offset, usz BitCount, typename ValueType>
constexpr Bitfield<T, Offset, BitCount, ValueType>& Bitfield<T, Offset, BitCount, ValueType>::operator=(ValueType v)
{
	Store(v);

	return *this;
}