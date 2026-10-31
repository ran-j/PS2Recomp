#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Single-producer/single-consumer ring of stereo 16-bit frames: the IOP mixes into it on the EE
// thread and the host audio callback drains it, so neither side ever blocks the other.
class Spu2FrameRing
{
public:
    Spu2FrameRing(size_t capacityFrames, size_t prebufferFrames, size_t maxLatencyFrames);

    // Producer side. Frames that do not fit are dropped: the host is behind and old audio is stale anyway.
    size_t push(const int16_t *samples, size_t frames);

    // Consumer side. Always fills `frames`, padding with silence. After running dry it waits for
    // `prebufferFrames` before resuming, so a slow producer gives gaps instead of a buzz of tiny chunks;
    // beyond `maxLatencyFrames` queued, the oldest frames are skipped to keep audio in sync.
    size_t pop(int16_t *out, size_t frames);

    [[nodiscard]] size_t available() const noexcept;
    [[nodiscard]] uint64_t underruns() const noexcept { return m_underruns.load(std::memory_order_relaxed); }

private:
    std::vector<int16_t> m_samples;
    size_t m_capacity;
    size_t m_prebuffer;
    size_t m_maxLatency;
    std::atomic<size_t> m_readIndex{0};
    std::atomic<size_t> m_writeIndex{0};
    bool m_starving = true;
    std::atomic<uint64_t> m_underruns{0};
};

// Plays SPU2 output through the host audio device.
class Spu2AudioOutput
{
public:
    Spu2AudioOutput();
    ~Spu2AudioOutput();

    Spu2AudioOutput(const Spu2AudioOutput &) = delete;
    Spu2AudioOutput &operator=(const Spu2AudioOutput &) = delete;

    void submit(const int16_t *samples, size_t frames);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
