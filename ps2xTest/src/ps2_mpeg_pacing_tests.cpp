#include "MiniTest.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_syscalls.h"
#include "Stubs/MPEG.h"
#include "Stubs/MPEG_internal.h"
#include "mpeg_pacing_fixture_m2v.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

using namespace ps2_syscalls;

namespace
{
    struct TestEnv
    {
        std::vector<uint8_t> rdram;
        PS2Runtime runtime;

        TestEnv() : rdram(PS2_RAM_SIZE, 0u)
        {
        }
    };

    template <typename Predicate>
    bool waitUntil(Predicate pred, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (pred())
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return pred();
    }

    void cleanupRuntime(TestEnv &env)
    {
        env.runtime.requestStop();
        notifyRuntimeStop();
    }

    void setRegU32(R5900Context &ctx, int reg, uint32_t value)
    {
        SET_GPR_U32(&ctx, reg, value);
    }

    const size_t kMpegPacingFixtureM2vLen = kMpegPacingFixtureM2v_len;
}

void register_ps2_mpeg_pacing_tests()
{
    MiniTest::Case("PS2MpegPacing", [](TestCase &tc)
    {
        tc.Run("frame due-tick is the exact, drift-free frame-rate rational", [](TestCase &t)
        {
            using ps2_stubs::mpegFrameDueTick;
            const uint64_t base = 1000u;
            const uint64_t hz   = 60u;

            // N=0 is due immediately at base.
            t.Equals(mpegFrameDueTick(base, 0u, hz, 30000, 1001), base, "frame 0 due at base");

            // Exact closed-form for several N at NTSC 29.97 (30000/1001).
            for (uint64_t n : {1u, 2u, 30u, 100u, 4321u})
            {
                const uint64_t expected = base + (n * hz * 1001ull) / 30000ull;
                t.Equals(mpegFrameDueTick(base, n, hz, 30000, 1001), expected,
                         "due(N) must equal the closed-form rational");
            }

            // Drift-free over a long movie: computed fresh from N, never accumulated. Pin
            // monotonic non-decreasing AND equality with the closed form at 10000 frames.
            uint64_t prev = base;
            for (uint64_t n = 1u; n <= 10000u; ++n)
            {
                const uint64_t due = mpegFrameDueTick(base, n, hz, 30000, 1001);
                t.IsTrue(due >= prev, "due ticks must be monotonic");
                prev = due;
            }
            t.Equals(mpegFrameDueTick(base, 10000u, hz, 30000, 1001),
                     base + (10000ull * hz * 1001ull) / 30000ull,
                     "due(10000) exact -- no accumulator drift");

            // Fallback / guard: unknown or degenerate frame rate returns base (no div-by-zero,
            // no pacing) -- the inert path for the synthetic/stub picture path.
            t.Equals(mpegFrameDueTick(base, 5u, hz, 0, 0), base, "unknown fps => no pacing");
            t.Equals(mpegFrameDueTick(base, 5u, hz, 30000, 0), base, "bad den => no pacing");
        });

        tc.Run("frame-rate sanity clamp accepts real rates and rejects garbage", [](TestCase &t)
        {
            using ps2_stubs::mpegFrameRateIsSane;
            // Real movie rates accepted.
            t.IsTrue(mpegFrameRateIsSane(30000, 1001), "29.97 accepted");
            t.IsTrue(mpegFrameRateIsSane(24, 1),       "24 accepted");
            t.IsTrue(mpegFrameRateIsSane(25, 1),       "25 (PAL) accepted");
            t.IsTrue(mpegFrameRateIsSane(30, 1),       "30 accepted");
            t.IsTrue(mpegFrameRateIsSane(60, 1),       "60 accepted");
            // Garbage / out-of-range rejected -> caller keeps the NTSC fallback.
            t.IsFalse(mpegFrameRateIsSane(0, 0),    "0/0 rejected");
            t.IsFalse(mpegFrameRateIsSane(30000, 0),"bad den rejected");
            t.IsFalse(mpegFrameRateIsSane(-30, 1),  "negative rejected");
            t.IsFalse(mpegFrameRateIsSane(5, 1),    "5fps (<10) rejected");
            t.IsFalse(mpegFrameRateIsSane(200, 1),  "200fps (>120) rejected");
            t.IsFalse(mpegFrameRateIsSane(1, 1000), "0.001fps rejected");
        });

        tc.Run("park blocks until the due tick, then returns", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            // Prime the worker and read a current tick.
            const uint64_t start = ps2_syscalls::WaitForNextVSyncTick(env.rdram.data(), &env.runtime);
            const uint64_t due = start + 3u; // ~50 ms out; below the 8-tick cap

            std::atomic<bool> done{false};
            const auto t0 = std::chrono::steady_clock::now();
            std::thread th([&]() {
                ps2_stubs::mpegParkUntilDueTick(env.rdram.data(), &env.runtime, due);
                done.store(true, std::memory_order_release);
            });

            // Must actually block (not return instantly like the unpaced bug would).
            const bool finished = waitUntil([&]() { return done.load(std::memory_order_acquire); },
                                            std::chrono::milliseconds(2000));
            th.join();
            const auto elapsed = std::chrono::steady_clock::now() - t0;
            t.IsTrue(finished, "park must return once the due tick is reached");
            // This wall-clock window rides the shared vsync worker: it carries slack for the
            // worker start-race, not correctness -- the tick-count assertion below (tick reached
            // the due tick) is the real discriminator.
            t.IsTrue(elapsed >= std::chrono::milliseconds(25),
                     "park must wait ~3 vblanks, not return immediately");
            t.IsTrue(ps2_syscalls::GetCurrentVSyncTick() >= due, "tick reached due before return");

            cleanupRuntime(env);
        });

        tc.Run("park releases promptly on runtime stop", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            const uint64_t start = ps2_syscalls::WaitForNextVSyncTick(env.rdram.data(), &env.runtime);
            const uint64_t unreachable = start + 1000000u; // never reached in test time

            std::atomic<bool> done{false};
            std::thread th([&]() {
                ps2_stubs::mpegParkUntilDueTick(env.rdram.data(), &env.runtime, unreachable);
                done.store(true, std::memory_order_release);
            });

            env.runtime.requestStop();     // latches isStopRequested()
            notifyRuntimeStop();           // wakes the vsync worker
            const bool released = waitUntil([&]() { return done.load(std::memory_order_acquire); },
                                            std::chrono::milliseconds(2000));
            th.join();
            // Without the isStopRequested() guard in mpegParkUntilDueTick, this hangs forever
            // (tick never reaches `unreachable`, WaitForNextVSyncTick returns immediately each
            // iteration) and the test times out -- which is exactly the guard we are pinning.
            // With the per-call cap present, the discriminator still holds: after requestStop()
            // the worker stops advancing the tick, so the frozen tick stays below the capped
            // due tick and -- absent the isStopRequested() guard -- the loop would spin forever.
            t.IsTrue(released, "park must exit promptly when the runtime is stopped");

            cleanupRuntime(env);
        });

        tc.Run("park is hard-capped and cannot stall on a pathological due tick", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            const uint64_t start = ps2_syscalls::WaitForNextVSyncTick(env.rdram.data(), &env.runtime);
            const uint64_t absurd = start + 1000000u; // far beyond the cap; unreachable in test time

            std::atomic<bool> done{false};
            std::thread th([&]() {
                ps2_stubs::mpegParkUntilDueTick(env.rdram.data(), &env.runtime, absurd);
                done.store(true, std::memory_order_release);
            });
            // Worker keeps running; the cap (8 vblanks) must bound the park despite the absurd due.
            const bool finished = waitUntil([&]() { return done.load(std::memory_order_acquire); },
                                            std::chrono::milliseconds(2000));
            th.join();
            t.IsTrue(finished, "an uncapped park would never return here; the cap must bound it");
            // Returned after ~the cap, not the absurd target.
            t.IsTrue(ps2_syscalls::GetCurrentVSyncTick() < start + 64u,
                     "park must return within a few vblanks of the cap, not chase the absurd due tick");
            cleanupRuntime(env);
        });

        tc.Run("pacing park holds no global MPEG mutex (concurrent stub calls make progress)",
               [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            ps2_stubs::resetMpegStubState();
            const uint64_t start = ps2_syscalls::WaitForNextVSyncTick(env.rdram.data(), &env.runtime);
            const uint64_t due = start + 5u; // ~83ms; below the cap so the park runs its full length

            std::atomic<bool> parkDone{false};
            std::thread th([&]() {
                ps2_stubs::mpegParkUntilDueTick(env.rdram.data(), &env.runtime, due);
                parkDone.store(true, std::memory_order_release);
            });

            int resets = 0;
            while (!parkDone.load(std::memory_order_acquire))
            {
                ps2_stubs::resetMpegStubState(); // acquires g_mpeg_stub_mutex each call
                ++resets;
                std::this_thread::yield();
            }
            th.join();
            // With the park correctly outside the lock, thousands of resets complete during it; if
            // the park held g_mpeg_stub_mutex, each reset would block until the park ended and this
            // count would be ~0-1.
            t.IsTrue(resets >= 3, "stub calls that take g_mpeg_stub_mutex must progress during a park");
            cleanupRuntime(env);
        });

        tc.Run("sceMpegGetPicture meters real frame delivery to the stream's encoded rate",
               [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            ps2_stubs::resetMpegStubState();
            uint8_t *rdram = env.rdram.data();
            const uint32_t mpegAddr  = 0x00100000u;
            const uint32_t esAddr    = 0x00200000u;
            const uint32_t imageAddr = 0x00300000u;

            (void)ps2_syscalls::WaitForNextVSyncTick(rdram, &env.runtime); // prime the worker

            // Locate MPEG-2 picture start codes (00 00 01 00) to split the ES per access unit.
            const uint8_t *es = kMpegPacingFixtureM2v;
            const size_t esLen = kMpegPacingFixtureM2vLen;
            std::vector<size_t> pic;
            for (size_t i = 0; i + 3 < esLen; ++i)
                if (es[i] == 0 && es[i+1] == 0 && es[i+2] == 1 && es[i+3] == 0x00) pic.push_back(i);
            t.IsTrue(pic.size() >= 5, "fixture must contain several coded pictures");

            auto feed = [&](size_t from, size_t to) {
                const size_t n = to - from;
                std::memcpy(rdram + esAddr, es + from, n);
                R5900Context ctx{};
                setRegU32(ctx, 4, mpegAddr);
                setRegU32(ctx, 5, esAddr);
                setRegU32(ctx, 6, static_cast<uint32_t>(n));
                ps2_stubs::sceMpegAddBs(rdram, &ctx, &env.runtime); // decodes synchronously
            };
            feed(0, pic[1]);                                   // headers + picture 0
            for (size_t k = 1; k + 1 < pic.size(); ++k) feed(pic[k], pic[k+1]);
            feed(pic.back(), esLen);                           // last picture

            auto getPic = [&]() {
                R5900Context ctx{};
                setRegU32(ctx, 4, mpegAddr);
                setRegU32(ctx, 5, imageAddr);
                setRegU32(ctx, 6, 0u);
                ps2_stubs::sceMpegGetPicture(rdram, &ctx, &env.runtime);
            };

            const int n = static_cast<int>(pic.size());        // frames decoded == pictures fed
            std::vector<uint64_t> tickAt;
            tickAt.reserve(n);
            for (int f = 0; f < n; ++f) { getPic(); tickAt.push_back(ps2_syscalls::GetCurrentVSyncTick()); }

            // Deterministic latch pin (jitter-free): after real frames are drained the handle
            // must have latched the fixture's true 25fps from the decoder, NOT the 30000/1001
            // fallback. This -- not the wall-clock span below -- is what discriminates a working
            // getFrameRate()/sane-clamp from a broken one: the span floor for 25fps (12 ticks) and
            // the fallback (10 ticks) differ by only ~2 ticks, which vsync-worker jitter swamps, so
            // span alone cannot separate them. See the VALIDATION latch revert-check.
            int latchNum = 0, latchDen = 0;
            const bool latched = ps2_stubs::mpegLatchedFrameRate(mpegAddr, latchNum, latchDen);
            t.IsTrue(latched,
                     "stream frame rate must latch from the decoder once real frames are served");
            t.Equals(latchNum, 25, "latched frame-rate numerator must be the fixture's real 25fps");
            t.Equals(latchDen, 1, "latched frame-rate denominator must be 1 (25/1, not the fallback)");

            const uint64_t span = tickAt.back() - tickAt.front();
            // End-to-end pacing pin (NOT the latch discriminator -- that is the direct
            // mpegLatchedFrameRate assertion above). Lower bound: paced delivery must span the
            // encoded rate's ideal (floor((n-1)*60/25) = 12 for 6 frames), computed from the same
            // drift-free due-tick math the runtime uses -- so a wholesale UNPACED regression, which
            // drains all frames within a tick or two, fails it. It does not separate 25fps from the
            // 30000/1001 fallback: their span floors (12 vs 10) differ by less than the vsync-worker
            // jitter, so the latch is pinned directly above, not here.
            //
            // Deliberately no upper ceiling. Over-throttling shows up only as extra elapsed vsync
            // ticks, and the park rides the real worker, so any upper bound on the measured span is
            // wall-clock -- it would be the suite's one non-deterministic assertion. The only
            // ceiling derivable purely from pinned constants is
            // (n-1)*kMpegPacingMaxParkVsyncTicks = 40 for n=6, which sits ABOVE both a legitimate
            // span (~12) and the gross-over-throttle failure it would need to catch (every gap
            // parking to the cap, span ~= 40): a constant-derived ceiling is necessarily >= the
            // worst buggy span and so discriminates nothing. The rate is instead pinned
            // deterministically by the latch above (a too-slow rate fails it), and the schedule
            // math and single-park bound by the standalone due-tick and per-call-cap cases -- so
            // every assertion here stays deterministic.
            const uint64_t expected =
                ps2_stubs::mpegFrameDueTick(0u, static_cast<uint64_t>(n - 1), 60u, 25, 1);
            t.IsTrue(span + 1 >= expected,
                     "paced delivery must span the encoded rate (~12 vblanks), not drain instantly "
                     "(the unpaced bug)");

            // No-frame path is inert: the queue is now empty; end the stream so further GetPicture
            // calls take the no-frame/blank path, and assert they are NOT paced.
            ps2_stubs::notifyMpegCdStreamEof();
            const uint64_t t0 = ps2_syscalls::GetCurrentVSyncTick();
            for (int f = 0; f < 4; ++f) getPic();
            t.IsTrue(ps2_syscalls::GetCurrentVSyncTick() - t0 <= 2u,
                     "no-frame path must not be paced (no per-call park once frames are exhausted/ended)");

            cleanupRuntime(env);
        });

        tc.Run("duplicate-last-frame path is inert (re-served without a pacing park)",
               [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            ps2_stubs::resetMpegStubState();
            uint8_t *rdram = env.rdram.data();
            const uint32_t mpegAddr  = 0x00100000u;
            const uint32_t esAddr    = 0x00200000u;
            const uint32_t imageAddr = 0x00300000u;

            (void)ps2_syscalls::WaitForNextVSyncTick(rdram, &env.runtime); // prime the worker

            // Feed the whole fixture and drain every real decoded frame -- exactly like the
            // encoded-rate case -- WITHOUT flagging CD-stream EOF. The handle then holds the
            // precise duplicate-branch precondition: empty queue, sawInput, hasLastFrame,
            // picturesServed > 0, stream not ended, and no CD-stream EOF seen.
            const uint8_t *es = kMpegPacingFixtureM2v;
            const size_t esLen = kMpegPacingFixtureM2vLen;
            std::vector<size_t> pic;
            for (size_t i = 0; i + 3 < esLen; ++i)
                if (es[i] == 0 && es[i+1] == 0 && es[i+2] == 1 && es[i+3] == 0x00) pic.push_back(i);
            t.IsTrue(pic.size() >= 5, "fixture must contain several coded pictures");

            auto feed = [&](size_t from, size_t to) {
                const size_t nbytes = to - from;
                std::memcpy(rdram + esAddr, es + from, nbytes);
                R5900Context ctx{};
                setRegU32(ctx, 4, mpegAddr);
                setRegU32(ctx, 5, esAddr);
                setRegU32(ctx, 6, static_cast<uint32_t>(nbytes));
                ps2_stubs::sceMpegAddBs(rdram, &ctx, &env.runtime);
            };
            feed(0, pic[1]);
            for (size_t k = 1; k + 1 < pic.size(); ++k) feed(pic[k], pic[k+1]);
            feed(pic.back(), esLen);

            auto getPic = [&]() {
                R5900Context ctx{};
                setRegU32(ctx, 4, mpegAddr);
                setRegU32(ctx, 5, imageAddr);
                setRegU32(ctx, 6, 0u);
                ps2_stubs::sceMpegGetPicture(rdram, &ctx, &env.runtime);
            };

            const int n = static_cast<int>(pic.size());     // real frames == pictures fed
            for (int f = 0; f < n; ++f) getPic();            // drain all real frames (each paced)

            // Queue empty, stream neither ended nor EOF-flagged: the next GetPicture must fall
            // through the bounded no-frame wait (~64 ms) and re-serve the last frame -- the
            // duplicate-last-frame branch, which must NOT arm pacing.
            std::memset(rdram + imageAddr, 0, 64u);          // so a written frame is observable
            const uint64_t before = ps2_syscalls::GetCurrentVSyncTick();
            getPic();
            const uint64_t after = ps2_syscalls::GetCurrentVSyncTick();

            // Confirm the duplicate branch ran (not the blank/no-frame branch): it re-serves the
            // last frame image (haveFrame == true), whereas the no-frame branch with frameCount>0
            // writes nothing; and it re-serves at frame index == frames already served.
            uint32_t frameField = 0u;
            std::memcpy(&frameField, rdram + mpegAddr + 0x08u, sizeof(frameField));
            t.Equals(frameField, static_cast<uint32_t>(n),
                     "duplicate branch re-serves at the next frame index (== frames served)");
            bool imageWritten = false;
            for (size_t i = 0; i < 64u; ++i)
                if (rdram[imageAddr + i] != 0u) { imageWritten = true; break; }
            t.IsTrue(imageWritten, "duplicate branch must re-serve the last frame image");

            // The pin: the duplicate call is bounded by the no-frame wait (~4 vblanks) and is NOT
            // extended by a pacing park. A forced park on this branch (see the VALIDATION
            // revert-check) runs to the per-call cap (8 vblanks) on top of the wait -- ~12 ticks
            // -- so an 8-tick ceiling cleanly separates the inert path (~4) from a paced one.
            t.IsTrue(after - before <= 8u,
                     "duplicate-last-frame path must not add a pacing park");

            cleanupRuntime(env);
        });
    });
}
