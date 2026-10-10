#pragma once
#if defined(_MSC_VER)
#define PS2VU_INLINE __forceinline
#else
#define PS2VU_INLINE inline __attribute__((always_inline))
#endif
