#include "MiniTest.h"
#include "ps2_log.h"
#include "ps2_runtime.h"
#include "runtime/ps2_diag.h"
#include "runtime/ps2_guestwatch.h"
#include "runtime/ps2_gs_gpu.h"
#include "runtime/ps2_gs_rasterizer.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Regression coverage for the opt-in "runtime observability" diagnostics
// layer: the should_log throttle, the poll-based guest-memory watch probe
// (including writer-PC capture), the GS CLUT-dump swizzle fix, and the
// [gs:image] transfer probe.
//
// The diagnostics statics (should_log/ps2_watch registry; the clut-dump
// "seen keys" set and [gs:image] dbp tracker) are process-lifetime, so each
// test is order-robust: watch tests reset via clearWatches(), CLUT tests use
// a distinct cbp, the [gs:image] test picks a unique DBP, and every test
// leaves diagnostics disabled, watches cleared, and the log ring cleared.

namespace
{
    void writeLE32(std::vector<uint8_t> &rdram, uint32_t addr, uint32_t value)
    {
        std::memcpy(rdram.data() + addr, &value, sizeof(value));
    }

    std::vector<ps2_log::RuntimeLogEntry> snapshotLog()
    {
        return ps2_log::snapshot_runtime_log_entries();
    }

    size_t countTagged(const char *tag)
    {
        size_t n = 0;
        for (const auto &entry : snapshotLog())
        {
            if (entry.text.find(tag) != std::string::npos)
                ++n;
        }
        return n;
    }

    std::vector<std::string> collectTagged(const char *tag)
    {
        std::vector<std::string> lines;
        for (const auto &entry : snapshotLog())
        {
            if (entry.text.find(tag) != std::string::npos)
                lines.push_back(entry.text);
        }
        return lines;
    }

    // Parses the integer following `key` (e.g. "idx=") in `text`. Returns -1
    // when the key isn't present. Relies on std::stol stopping at the first
    // non-digit character, so it works fine on "idx=5 r=13 ...".
    long parseField(const std::string &text, const std::string &key)
    {
        const size_t pos = text.find(key);
        if (pos == std::string::npos)
            return -1;
        return std::stol(text.substr(pos + key.size()));
    }

    uint64_t makeGifTagLocal(uint16_t nloop, uint8_t flg, uint8_t nreg, bool eop = true)
    {
        uint64_t tag = static_cast<uint64_t>(nloop & 0x7FFFu);
        if (eop)
            tag |= (1ull << 15);
        tag |= (static_cast<uint64_t>(flg & 0x3u) << 58);
        tag |= (static_cast<uint64_t>(nreg & 0xFu) << 60);
        return tag;
    }

    void appendU64Local(std::vector<uint8_t> &dst, uint64_t value)
    {
        const size_t pos = dst.size();
        dst.resize(pos + sizeof(uint64_t));
        std::memcpy(dst.data() + pos, &value, sizeof(uint64_t));
    }
}

void register_ps2_observability_tests()
{
    MiniTest::Case("PS2 Runtime Observability", [](TestCase &tc)
    {
        tc.Run("should_log throttle is true for exactly 19 of the first 2000 counts", [](TestCase &t)
        {
            int trueCount = 0;
            std::vector<uint64_t> trueValues;
            for (uint64_t c = 0; c < 2000; ++c)
            {
                if (ps2_diag::should_log(c, 16, 600))
                {
                    ++trueCount;
                    trueValues.push_back(c);
                }
            }

            t.Equals(trueCount, 19, "should_log(c,16,600) should be true 16 + floor((2000-16)/600) == 19 times over c=0..1999");

            std::vector<uint64_t> expected;
            for (uint64_t c = 0; c < 16; ++c)
                expected.push_back(c);
            expected.push_back(600);
            expected.push_back(1200);
            expected.push_back(1800);

            t.Equals(trueValues, expected, "should_log true set should be exactly {0..15, 600, 1200, 1800}");

            // should_log() itself is stateless, but keep every test in this
            // file leaving the shared diagnostics/watch/log state clean.
            ps2_watch::clearWatches();
            ps2_diag::set_enabled_for_test(false);
            ps2_log::clear_runtime_log_entries();
        });

        tc.Run("watch: unregistered probes never log", [](TestCase &t)
        {
            ps2_watch::clearWatches();
            ps2_diag::set_enabled_for_test(true);
            ps2_log::clear_runtime_log_entries();

            std::vector<uint8_t> rdram(64 * 1024, 0);
            for (uint32_t i = 0; i < 8; ++i)
            {
                writeLE32(rdram, 0x1000, 0x1000u + i * 37u);
                ps2_watch::pollWatches(rdram.data());
            }

            t.Equals(countTagged("[watch]"), static_cast<size_t>(0),
                     "pollWatches with no registered watches should never emit [watch] lines");

            ps2_watch::clearWatches();
            ps2_diag::set_enabled_for_test(false);
            ps2_log::clear_runtime_log_entries();
        });

        tc.Run("watch: two value changes produce exactly two log lines", [](TestCase &t)
        {
            ps2_watch::clearWatches();
            ps2_diag::set_enabled_for_test(true);
            ps2_log::clear_runtime_log_entries();

            std::vector<uint8_t> rdram(64 * 1024, 0);
            ps2_watch::addWatch(0x1000, 4, "testWatch");

            ps2_watch::pollWatches(rdram.data()); // seed: must not log
            t.Equals(countTagged("[watch]"), static_cast<size_t>(0), "the first poll after registration should only seed, not log");

            writeLE32(rdram, 0x1000, 0x0000BEEFu);
            ps2_watch::pollWatches(rdram.data());

            writeLE32(rdram, 0x1000, 0x0000CAFEu);
            ps2_watch::pollWatches(rdram.data());

            const auto lines = collectTagged("[watch]");
            t.Equals(lines.size(), static_cast<size_t>(2), "exactly two observed changes should produce exactly two [watch] lines");

            if (!lines.empty())
            {
                const std::string &first = lines.front();
                t.IsTrue(first.find("testWatch") != std::string::npos, "first line should contain the watch label");
                t.IsTrue(first.find("0x1000") != std::string::npos, "first line should contain the watch address");
                t.IsTrue(first.find("(change #1)") != std::string::npos, "first line should report change #1");
                t.IsTrue(first.find("0x0") != std::string::npos, "first line should report the old value (0) in hex");
                t.IsTrue(first.find("0xbeef") != std::string::npos, "first line should report the new value (0xBEEF) in hex");
            }
            if (lines.size() > 1)
            {
                const std::string &second = lines[1];
                t.IsTrue(second.find("(change #2)") != std::string::npos, "second line should report change #2");
                t.IsTrue(second.find("0xbeef") != std::string::npos, "second line should report the prior value (0xBEEF) as old");
                t.IsTrue(second.find("0xcafe") != std::string::npos, "second line should report the new value (0xCAFE)");
            }

            ps2_watch::clearWatches();
            ps2_diag::set_enabled_for_test(false);
            ps2_log::clear_runtime_log_entries();
        });

        tc.Run("watch: 2000 changes throttle to exactly 19 log lines", [](TestCase &t)
        {
            ps2_watch::clearWatches();
            ps2_diag::set_enabled_for_test(true);
            ps2_log::clear_runtime_log_entries();

            std::vector<uint8_t> rdram(64 * 1024, 0);
            ps2_watch::addWatch(0x1000, 4, "bulkWatch");
            ps2_watch::pollWatches(rdram.data()); // seed

            ps2_log::clear_runtime_log_entries();
            for (uint32_t i = 1; i <= 2000; ++i)
            {
                writeLE32(rdram, 0x1000, i);
                ps2_watch::pollWatches(rdram.data());
            }

            t.Equals(countTagged("[watch]"), static_cast<size_t>(19),
                     "2000 changes through should_log(c,16,600) should log exactly 19 times (matches the should_log bound-math test)");

            ps2_watch::clearWatches();
            ps2_diag::set_enabled_for_test(false);
            ps2_log::clear_runtime_log_entries();
        });

        tc.Run("watch: disabled diagnostics gate suppresses all logging", [](TestCase &t)
        {
            ps2_watch::clearWatches();
            ps2_diag::set_enabled_for_test(true);
            ps2_watch::addWatch(0x3000, 4, "gateWatch");
            ps2_diag::set_enabled_for_test(false);
            ps2_log::clear_runtime_log_entries();

            std::vector<uint8_t> rdram(64 * 1024, 0);
            writeLE32(rdram, 0x3000, 0x00001234u);
            ps2_watch::pollWatches(rdram.data());

            t.Equals(countTagged("[watch]"), static_cast<size_t>(0),
                     "pollWatches should no-op entirely while the diagnostics gate is disabled");

            ps2_watch::clearWatches();
            ps2_diag::set_enabled_for_test(false);
            ps2_log::clear_runtime_log_entries();
        });

        tc.Run("watch: writer PC is captured and attached to the next logged change", [](TestCase &t)
        {
            ps2_watch::clearWatches();
            ps2_diag::set_enabled_for_test(true);
            ps2_log::clear_runtime_log_entries();

            std::vector<uint8_t> rdram(64 * 1024, 0);
            ps2_watch::addWatch(0x2000, 4, "pcWatch");
            ps2_watch::pollWatches(rdram.data()); // seed
            ps2_log::clear_runtime_log_entries();

            R5900Context ctx{};
            ctx.pc = 0x00123450u;

            const uint32_t newValue = 0xDEADBEEFu;
            writeLE32(rdram, 0x2000, newValue);
            ps2TraceGuestWrite(rdram.data(), 0x2000, 4, newValue, 0, "WRITE32", &ctx);

            ps2_watch::pollWatches(rdram.data());

            const auto lines = collectTagged("[watch]");
            t.Equals(lines.size(), static_cast<size_t>(1), "a single guest write followed by one poll should log exactly one change");
            if (!lines.empty())
            {
                t.IsTrue(lines.front().find("pc=0x123450") != std::string::npos,
                         "the logged change should carry the writer PC captured via ps2TraceGuestWrite");
            }

            ps2_watch::clearWatches();
            ps2_diag::set_enabled_for_test(false);
            ps2_log::clear_runtime_log_entries();
        });

        tc.Run("watch: PS2X_WATCH env spec arms a watch that fires via the store hook", [](TestCase &t)
        {
            ps2_watch::clearWatches();
            ps2_diag::set_enabled_for_test(false);
            ps2_log::clear_runtime_log_entries();

            // armWatchesFromEnv parses the spec string directly (no env mutation).
            const size_t armed = ps2_watch::armWatchesFromEnv("0x2000:4:envWatch");
            t.Equals(armed, static_cast<size_t>(1), "a single-entry PS2X_WATCH spec should arm exactly one watch");

            // Mirror the boot hook: arming enables the diagnostics gate.
            ps2_diag::set_enabled(true);

            std::vector<uint8_t> rdram(64 * 1024, 0);
            ps2_watch::pollWatches(rdram.data()); // seed
            ps2_log::clear_runtime_log_entries();

            R5900Context ctx{};
            ctx.pc = 0x00ABCDE0u;

            const uint32_t newValue = 0x12345678u;
            writeLE32(rdram, 0x2000, newValue);
            ps2TraceGuestWrite(rdram.data(), 0x2000, 4, newValue, 0, "WRITE32", &ctx);

            ps2_watch::pollWatches(rdram.data());

            const auto lines = collectTagged("[watch]");
            t.Equals(lines.size(), static_cast<size_t>(1), "the env-armed watch should log exactly one change");
            if (!lines.empty())
            {
                t.IsTrue(lines.front().find("envWatch") != std::string::npos,
                         "the logged line should carry the label parsed from the PS2X_WATCH spec");
                t.IsTrue(lines.front().find("pc=0xabcde0") != std::string::npos,
                         "the env-armed watch should carry the writer PC captured via ps2TraceGuestWrite");
            }

            ps2_watch::clearWatches();
            ps2_diag::set_enabled_for_test(false);
            ps2_log::clear_runtime_log_entries();
        });

        tc.Run("clut-dump: CSM1 T4 dump applies the swizzle to the logical index", [](TestCase &t)
        {
            ps2_diag::set_enabled_for_test(true);
            ps2_log::clear_runtime_log_entries();

            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            constexpr uint32_t kCbp = 0x100u; // distinct from every other clut-dump test in this suite
            for (uint32_t p = 0; p < 32; ++p)
            {
                gs.WriteVram(GS_PSM_CT32, kCbp, 1u, p & 0xFu, p >> 4, p);
            }

            ps2_log::clear_runtime_log_entries();
            GSRasterizer raster;
            raster.lookupCLUT(&gs, 0u, kCbp, GS_PSM_CT32, /*csm=*/0u, /*csa=*/0u, GS_PSM_T4);

            const auto entries = collectTagged("[gs:clut-dump]   idx=");
            t.Equals(entries.size(), static_cast<size_t>(16), "a T4/CSM1 dump should emit 16 entry lines");

            bool swizzleOk = true;
            for (const auto &line : entries)
            {
                const long idx = parseField(line, "idx=");
                const long r = parseField(line, "r=");
                if (idx < 0 || r < 0)
                {
                    swizzleOk = false;
                    continue;
                }
                const long expected = (idx < 8) ? idx : (idx + 8);
                if (r != expected)
                    swizzleOk = false;
            }
            t.IsTrue(swizzleOk, "CSM1 dump must swizzle logical indices 8-15 onto physical slots 16-23 (r == idx for 0-7, r == idx+8 for 8-15)");

            ps2_diag::set_enabled_for_test(false);
            ps2_log::clear_runtime_log_entries();
        });

        tc.Run("clut-dump: CSM2 T4 dump is linear (no swizzle)", [](TestCase &t)
        {
            // resolveClutIndex() only swizzles when csm == 0 (CSM1). For CSM2
            // (csm == 1) with csa == 0 the csa<<4 offset is still applied but
            // is zero, so the mapping is the identity -- confirmed by reading
            // ps2_gs_rasterizer.cpp's resolveClutIndex() (~line 227-253).
            ps2_diag::set_enabled_for_test(true);
            ps2_log::clear_runtime_log_entries();

            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            constexpr uint32_t kCbp = 0x200u; // distinct from every other clut-dump test in this suite
            for (uint32_t p = 0; p < 32; ++p)
            {
                gs.WriteVram(GS_PSM_CT32, kCbp, 1u, p & 0xFu, p >> 4, p);
            }

            ps2_log::clear_runtime_log_entries();
            GSRasterizer raster;
            raster.lookupCLUT(&gs, 0u, kCbp, GS_PSM_CT32, /*csm=*/1u, /*csa=*/0u, GS_PSM_T4);

            const auto entries = collectTagged("[gs:clut-dump]   idx=");
            t.Equals(entries.size(), static_cast<size_t>(16), "a T4/CSM2 dump should emit 16 entry lines");

            bool linearOk = true;
            for (const auto &line : entries)
            {
                const long idx = parseField(line, "idx=");
                const long r = parseField(line, "r=");
                if (idx < 0 || r < 0 || r != idx)
                    linearOk = false;
            }
            t.IsTrue(linearOk, "CSM2 (linear) dumps must not apply the CSM1 swizzle: r should equal idx for every entry");

            ps2_diag::set_enabled_for_test(false);
            ps2_log::clear_runtime_log_entries();
        });

        tc.Run("clut-dump: each (cbp, cpsm, csa, csm, sourcePsm) key is dumped only once", [](TestCase &t)
        {
            ps2_diag::set_enabled_for_test(true);
            ps2_log::clear_runtime_log_entries();

            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            constexpr uint32_t kCbp = 0x300u; // distinct from every other clut-dump test in this suite
            for (uint32_t p = 0; p < 32; ++p)
            {
                gs.WriteVram(GS_PSM_CT32, kCbp, 1u, p & 0xFu, p >> 4, p);
            }

            GSRasterizer raster;
            raster.lookupCLUT(&gs, 0u, kCbp, GS_PSM_CT32, 0u, 0u, GS_PSM_T4);
            t.IsTrue(countTagged("[gs:clut-dump]") > 0,
                     "the first lookupCLUT for a new (cbp,cpsm,csa) triple should emit a dump");

            ps2_log::clear_runtime_log_entries();
            raster.lookupCLUT(&gs, 0u, kCbp, GS_PSM_CT32, 0u, 0u, GS_PSM_T4);
            t.Equals(countTagged("[gs:clut-dump]"), static_cast<size_t>(0),
                     "repeating the same (cbp,cpsm,csa) triple must not add any new [gs:clut-dump] lines");

            // The dedup key must widen the (cbp,cpsm,csa) triple with csm (CLUT
            // addressing mode) and sourcePsm (index width): two palette views
            // that share the triple but differ in either field are genuinely
            // distinct dumps and must NOT be deduped away. Hold the triple fixed
            // and vary each of the two extra fields alone.

            // (a) vary csm alone (0 -> 1): a new addressing mode is a new dump.
            ps2_log::clear_runtime_log_entries();
            raster.lookupCLUT(&gs, 0u, kCbp, GS_PSM_CT32, 1u, 0u, GS_PSM_T4);
            t.IsTrue(countTagged("[gs:clut-dump]") > 0,
                     "varying csm alone (0->1) under a seen (cbp,cpsm,csa) triple must emit a new dump");

            // (b) vary sourcePsm alone (T4 -> T8): a new index width is a new dump.
            ps2_log::clear_runtime_log_entries();
            raster.lookupCLUT(&gs, 0u, kCbp, GS_PSM_CT32, 0u, 0u, GS_PSM_T8);
            t.IsTrue(countTagged("[gs:clut-dump]") > 0,
                     "varying sourcePsm alone (T4->T8) under a seen (cbp,cpsm,csa) triple must emit a new dump");

            ps2_diag::set_enabled_for_test(false);
            ps2_log::clear_runtime_log_entries();
        });

        tc.Run("gs:image: single IMAGE transfer logs the destination DBP and payload byte count", [](TestCase &t)
        {
            ps2_diag::set_enabled_for_test(true);
            ps2_log::clear_runtime_log_entries();

            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            // DBP chosen well outside the small values (0,1,10,64,150,...)
            // used by every other GS transfer test in this binary, so the
            // probe's "always log on DBP change" branch fires regardless of
            // suite run order (see ps2_gs_gpu.cpp processImageData()).
            constexpr uint32_t kDbp = 0x2000u;
            const uint64_t bitblt =
                (static_cast<uint64_t>(0u) << 0) |                          // SBP
                (static_cast<uint64_t>(1u) << 16) |                         // SBW
                (static_cast<uint64_t>(GS_PSM_CT32) << 24) |                // SPSM
                (static_cast<uint64_t>(kDbp) << 32) |                       // DBP
                (static_cast<uint64_t>(1u) << 48) |                        // DBW
                (static_cast<uint64_t>(GS_PSM_CT32) << 56);                 // DPSM
            gs.writeRegister(GS_REG_BITBLTBUF, bitblt);
            gs.writeRegister(GS_REG_TRXPOS, 0ull);
            gs.writeRegister(GS_REG_TRXREG, (4ull << 0) | (4ull << 32)); // 4x4 rect
            gs.writeRegister(GS_REG_TRXDIR, 0ull);                      // host -> local

            constexpr uint32_t kPixelCount = 16u;                    // 4x4
            constexpr uint32_t kPayloadBytes = kPixelCount * 4u;     // CT32 = 4 bytes/pixel = 64
            constexpr uint16_t kNloop = static_cast<uint16_t>(kPayloadBytes / 16u); // qwords of image payload

            std::vector<uint8_t> packet;
            appendU64Local(packet, makeGifTagLocal(kNloop, GIF_FMT_IMAGE, 0u, true));
            appendU64Local(packet, 0ull);
            packet.resize(packet.size() + kPayloadBytes, 0xABu);

            ps2_log::clear_runtime_log_entries();
            gs.processGIFPacket(packet.data(), static_cast<uint32_t>(packet.size()));

            const auto lines = collectTagged("[gs:image]");
            t.Equals(lines.size(), static_cast<size_t>(1), "a single IMAGE-mode GIF tag should log exactly one [gs:image] line");
            if (!lines.empty())
            {
                const std::string &line = lines.front();
                t.IsTrue(line.find("dbp=0x2000") != std::string::npos, "the logged line should contain the BITBLTBUF DBP we programmed");
                t.IsTrue(line.find("sizeBytes=64") != std::string::npos, "the logged line should contain the payload byte count");
            }

            ps2_diag::set_enabled_for_test(false);
            ps2_log::clear_runtime_log_entries();
        });
    });
}
