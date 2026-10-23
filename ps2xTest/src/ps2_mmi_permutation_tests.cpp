#include "MiniTest.h"
#include "ps2_runtime_macros.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

// Lane layouts from the EE Core Instruction Set Manual. Halfword h0 / word w0 is the least significant.
namespace
{
    using Halves = std::array<uint16_t, 8>;
    using Words = std::array<uint32_t, 4>;

    __m128i fromHalves(const Halves &h)
    {
        __m128i v;
        std::memcpy(&v, h.data(), sizeof(v));
        return v;
    }

    Halves toHalves(__m128i v)
    {
        Halves h{};
        std::memcpy(h.data(), &v, sizeof(v));
        return h;
    }

    __m128i fromWords(const Words &w)
    {
        __m128i v;
        std::memcpy(&v, w.data(), sizeof(v));
        return v;
    }

    Words toWords(__m128i v)
    {
        Words w{};
        std::memcpy(w.data(), &v, sizeof(v));
        return w;
    }

    const Halves kRtHalves = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    const Halves kRsHalves = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
    const Words kRtWords = {0xA0, 0xA1, 0xA2, 0xA3};

    template <typename T>
    void expectLanes(TestCase &t, const T &got, const T &want, const char *name)
    {
        for (size_t i = 0; i < want.size(); ++i)
            t.Equals(static_cast<uint32_t>(got[i]), static_cast<uint32_t>(want[i]),
                     std::string(name) + " lane " + std::to_string(i));
    }
}

void register_ps2_mmi_permutation_tests()
{
    MiniTest::Case("MMIPermutations", [](TestCase &tc)
    {
        tc.Run("word permutations", [](TestCase &t)
        {
            const __m128i rt = fromWords(kRtWords);
            expectLanes(t, toWords(PS2_PEXEW(rt)), Words{0xA2, 0xA1, 0xA0, 0xA3}, "PEXEW swaps w0 and w2");
            expectLanes(t, toWords(PS2_PEXCW(rt)), Words{0xA0, 0xA2, 0xA1, 0xA3}, "PEXCW swaps w1 and w2");
            expectLanes(t, toWords(PS2_PROT3W(rt)), Words{0xA1, 0xA2, 0xA0, 0xA3}, "PROT3W rotates w0..w2 and keeps w3");
        });

        tc.Run("halfword permutations act on each doubleword", [](TestCase &t)
        {
            const __m128i rt = fromHalves(kRtHalves);
            expectLanes(t, toHalves(PS2_PEXEH(rt)), Halves{2, 1, 0, 3, 6, 5, 4, 7}, "PEXEH swaps h0 and h2");
            expectLanes(t, toHalves(PS2_PEXCH(rt)), Halves{0, 2, 1, 3, 4, 6, 5, 7}, "PEXCH swaps h1 and h2");
            expectLanes(t, toHalves(PS2_PREVH(rt)), Halves{3, 2, 1, 0, 7, 6, 5, 4}, "PREVH reverses each doubleword");
        });

        tc.Run("halfword interleaves", [](TestCase &t)
        {
            const __m128i rs = fromHalves(kRsHalves);
            const __m128i rt = fromHalves(kRtHalves);
            expectLanes(t, toHalves(PS2_PINTH(rs, rt)), Halves{0x00, 0x14, 0x01, 0x15, 0x02, 0x16, 0x03, 0x17},
                        "PINTH interleaves rt's low halfwords with rs's high halfwords");
            expectLanes(t, toHalves(PS2_PINTEH(rs, rt)), Halves{0x00, 0x10, 0x02, 0x12, 0x04, 0x14, 0x06, 0x16},
                        "PINTEH interleaves the even halfwords of rt and rs");
        });
    });
}
