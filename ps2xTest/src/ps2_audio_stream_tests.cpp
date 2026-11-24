#include "MiniTest.h"
#include "ps2_audio_stream.h"
#include "ps2x/iop/iop_subsystem.h"
#include "../../ps2xIOP/tests/iop_compat_test_support.h"

#include <vector>

namespace
{
    std::vector<int16_t> rampFrames(size_t frames, int16_t first)
    {
        std::vector<int16_t> samples(frames * 2u);
        for (size_t i = 0; i < frames; ++i)
        {
            samples[i * 2u] = static_cast<int16_t>(first + static_cast<int16_t>(i));
            samples[i * 2u + 1u] = static_cast<int16_t>(-(first + static_cast<int16_t>(i)));
        }
        return samples;
    }
}

void register_ps2_audio_stream_tests()
{
    MiniTest::Case("Spu2FrameRing", [](TestCase &tc)
    {
        tc.Run("playback waits for the prebuffer, then keeps frame order", [](TestCase &t)
        {
            Spu2FrameRing ring(64u, 8u, 64u);
            std::vector<int16_t> out(8u, 7);
            const std::vector<int16_t> first = rampFrames(4u, 100);
            ring.push(first.data(), 4u);
            t.Equals(ring.pop(out.data(), 4u), size_t{0}, "a short queue must not start playback");
            t.Equals(out[0], int16_t{0}, "waiting for the prebuffer plays silence");

            const std::vector<int16_t> second = rampFrames(4u, 104);
            ring.push(second.data(), 4u);
            t.Equals(ring.pop(out.data(), 4u), size_t{4}, "a full prebuffer starts playback");
            t.Equals(out[0], int16_t{100}, "the oldest frame plays first");
            t.Equals(out[1], int16_t{-100}, "channels stay interleaved");
            t.Equals(out[6], int16_t{103}, "frames come out in order");
        });

        tc.Run("an underrun pads silence and rebuffers before resuming", [](TestCase &t)
        {
            Spu2FrameRing ring(64u, 8u, 64u);
            const std::vector<int16_t> frames = rampFrames(10u, 1);
            ring.push(frames.data(), 10u);
            std::vector<int16_t> out(32u, 7);
            t.Equals(ring.pop(out.data(), 16u), size_t{10}, "every queued frame should play");
            t.Equals(out[19], int16_t{-10}, "the last queued frame is intact");
            t.Equals(out[20], int16_t{0}, "the missing tail is silence");
            t.Equals(ring.underruns(), uint64_t{1}, "running dry is counted");

            const std::vector<int16_t> trickle = rampFrames(2u, 50);
            ring.push(trickle.data(), 2u);
            t.Equals(ring.pop(out.data(), 2u), size_t{0}, "a trickle after an underrun must wait for the prebuffer");
        });

        tc.Run("a queue far above its target plays faster, far below it slower, and near it unchanged", [](TestCase &t)
        {
            Spu2FrameRing ring(4096u, 8u, 4096u, 64u);
            const std::vector<int16_t> frames = rampFrames(256u, 0);
            ring.push(frames.data(), 256u);
            std::vector<int16_t> out(200u);
            t.Equals(ring.pop(out.data(), 100u), size_t{100}, "the callback is always filled");
            const size_t afterFast = ring.available();
            t.IsTrue(afterFast >= 150u && afterFast <= 152u, "a queue four times the target is consumed 5% faster than it plays");
            t.IsTrue(out[0] == 0 && out[2] > out[0] && out[198] > out[2], "faster playback keeps the ramp monotonic");

            Spu2FrameRing low(4096u, 8u, 4096u, 64u);
            low.push(frames.data(), 40u);
            t.Equals(low.pop(out.data(), 32u), size_t{32}, "the callback is filled from a short queue");
            t.Equals(low.available(), size_t{9}, "a queue below the target is stretched: 32 frames out, 31 consumed");

            Spu2FrameRing near(4096u, 8u, 4096u, 64u);
            near.push(frames.data(), 66u);
            t.Equals(near.pop(out.data(), 16u), size_t{16}, "playback starts");
            t.Equals(out[30], int16_t{15}, "within the dead band samples pass through untouched");
            t.Equals(near.available(), size_t{50}, "within the dead band one frame is consumed per frame played");
        });

        tc.Run("a full ring drops new frames and a long queue is trimmed to the latency cap", [](TestCase &t)
        {
            Spu2FrameRing ring(16u, 1u, 8u);
            const std::vector<int16_t> frames = rampFrames(20u, 0);
            t.Equals(ring.push(frames.data(), 20u), size_t{16}, "frames beyond capacity are dropped");
            std::vector<int16_t> out(8u);
            t.Equals(ring.pop(out.data(), 4u), size_t{4}, "playback starts");
            t.Equals(out[0], int16_t{8}, "audio more than the latency cap behind is skipped");
        });
    });

    MiniTest::Case("IopSpu2Output", [](TestCase &tc)
    {
        tc.Run("the IOP delivers SPU2 frames at 48 kHz of emulated time", [](TestCase &t)
        {
            iop_test::Host host;
            ps2x::iop::IopSubsystem subsystem(host);
            // 0.1 s of EE time at 294.912 MHz.
            for (int i = 0; i < 100; ++i)
                subsystem.runEeCycles(294912u);
            t.IsTrue(host.audioFrames >= 4800u - 512u && host.audioFrames <= 4800u,
                     "0.1 s should yield 4800 frames, flushed in blocks of at most 512");
        });
    });
}
