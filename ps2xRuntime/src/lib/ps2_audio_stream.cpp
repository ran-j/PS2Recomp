#include "ps2_audio_stream.h"

#include "ps2_host_backend.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

Spu2FrameRing::Spu2FrameRing(size_t capacityFrames, size_t prebufferFrames, size_t maxLatencyFrames)
    : m_samples((capacityFrames + 1u) * 2u),
      m_capacity(capacityFrames + 1u),
      m_prebuffer(std::min(prebufferFrames, capacityFrames)),
      m_maxLatency(std::min(maxLatencyFrames, capacityFrames))
{
}

size_t Spu2FrameRing::available() const noexcept
{
    const size_t write = m_writeIndex.load(std::memory_order_acquire);
    const size_t read = m_readIndex.load(std::memory_order_acquire);
    return (write + m_capacity - read) % m_capacity;
}

size_t Spu2FrameRing::push(const int16_t *samples, size_t frames)
{
    const size_t write = m_writeIndex.load(std::memory_order_relaxed);
    const size_t read = m_readIndex.load(std::memory_order_acquire);
    const size_t free = m_capacity - 1u - (write + m_capacity - read) % m_capacity;
    const size_t count = std::min(frames, free);
    for (size_t i = 0; i < count;)
    {
        const size_t index = (write + i) % m_capacity;
        const size_t run = std::min(count - i, m_capacity - index);
        std::memcpy(&m_samples[index * 2u], samples + i * 2u, run * 2u * sizeof(int16_t));
        i += run;
    }
    m_writeIndex.store((write + count) % m_capacity, std::memory_order_release);
    return count;
}

size_t Spu2FrameRing::pop(int16_t *out, size_t frames)
{
    size_t read = m_readIndex.load(std::memory_order_relaxed);
    const size_t write = m_writeIndex.load(std::memory_order_acquire);
    size_t queued = (write + m_capacity - read) % m_capacity;

    if (m_starving && queued < m_prebuffer)
    {
        std::memset(out, 0, frames * 2u * sizeof(int16_t));
        return 0u;
    }
    m_starving = false;

    if (queued > m_maxLatency)
    {
        read = (read + queued - m_maxLatency) % m_capacity;
        queued = m_maxLatency;
    }

    const size_t count = std::min(frames, queued);
    for (size_t i = 0; i < count;)
    {
        const size_t index = (read + i) % m_capacity;
        const size_t run = std::min(count - i, m_capacity - index);
        std::memcpy(out + i * 2u, &m_samples[index * 2u], run * 2u * sizeof(int16_t));
        i += run;
    }
    m_readIndex.store((read + count) % m_capacity, std::memory_order_release);

    if (count < frames)
    {
        std::memset(out + count * 2u, 0, (frames - count) * 2u * sizeof(int16_t));
        m_starving = true;
        m_underruns.fetch_add(1u, std::memory_order_relaxed);
    }
    return count;
}

namespace
{
    constexpr unsigned int kSpu2SampleRate = 48000u;
    // 250 ms of headroom, 40 ms to (re)start, and never more than 150 ms behind the game.
    constexpr size_t kRingFrames = kSpu2SampleRate / 4u;
    constexpr size_t kPrebufferFrames = kSpu2SampleRate / 25u;
    constexpr size_t kMaxLatencyFrames = kSpu2SampleRate * 3u / 20u;

    // raylib audio callbacks carry no user pointer.
    std::atomic<Spu2FrameRing *> g_activeRing{nullptr};

#if !defined(PLATFORM_VITA)
    void fillHostAudio(void *buffer, unsigned int frames)
    {
        Spu2FrameRing *ring = g_activeRing.load(std::memory_order_acquire);
        if (!ring)
        {
            std::memset(buffer, 0, static_cast<size_t>(frames) * 2u * sizeof(int16_t));
            return;
        }
        ring->pop(static_cast<int16_t *>(buffer), frames);
    }
#endif
}

struct Spu2AudioOutput::Impl
{
    Spu2FrameRing ring{kRingFrames, kPrebufferFrames, kMaxLatencyFrames};
    bool started = false;
    bool unavailable = false;
#if !defined(PLATFORM_VITA)
    AudioStream stream{};
#endif

    void start()
    {
#if defined(PLATFORM_VITA)
        unavailable = true;
#else
        // The device is opened with the window; until then audio is dropped, like on a muted TV.
        if (!IsAudioDeviceReady())
            return;
        stream = LoadAudioStream(kSpu2SampleRate, 16u, 2u);
        if (!IsAudioStreamValid(stream))
        {
            unavailable = true;
            return;
        }
        g_activeRing.store(&ring, std::memory_order_release);
        SetAudioStreamCallback(stream, &fillHostAudio);
        PlayAudioStream(stream);
        started = true;
#endif
    }

    ~Impl()
    {
#if !defined(PLATFORM_VITA)
        if (started)
        {
            StopAudioStream(stream);
            g_activeRing.store(nullptr, std::memory_order_release);
            UnloadAudioStream(stream);
        }
#endif
    }
};

Spu2AudioOutput::Spu2AudioOutput() : m_impl(std::make_unique<Impl>())
{
}

Spu2AudioOutput::~Spu2AudioOutput() = default;

namespace
{
    // PS2X_AUDIO_CAPTURE=<file.wav> records the SPU2 mix as the runtime produces it, independent
    // of the host device, so audio regressions can be compared offline.
    class Spu2WavCapture
    {
    public:
        Spu2WavCapture()
        {
            const char *path = std::getenv("PS2X_AUDIO_CAPTURE");
            if (!path || !*path)
                return;
            m_file = std::fopen(path, "wb");
            if (!m_file)
            {
                std::fprintf(stderr, "[audio] PS2X_AUDIO_CAPTURE: cannot open '%s' for writing\n", path);
                return;
            }
            writeHeader();
        }

        ~Spu2WavCapture()
        {
            if (m_file)
                std::fclose(m_file);
        }

        void append(const int16_t *samples, size_t frames)
        {
            if (!m_file || frames == 0u)
                return;
            std::fwrite(samples, sizeof(int16_t) * 2u, frames, m_file);
            m_dataBytes += static_cast<uint32_t>(frames * sizeof(int16_t) * 2u);
            // Keep the header valid so a killed process still leaves a playable file.
            writeHeader();
            std::fseek(m_file, 0, SEEK_END);
        }

    private:
        static void put32(uint8_t *p, uint32_t v)
        {
            p[0] = static_cast<uint8_t>(v);
            p[1] = static_cast<uint8_t>(v >> 8u);
            p[2] = static_cast<uint8_t>(v >> 16u);
            p[3] = static_cast<uint8_t>(v >> 24u);
        }

        void writeHeader()
        {
            uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
                             16, 0, 0, 0, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 16, 0,
                             'd', 'a', 't', 'a', 0, 0, 0, 0};
            put32(h + 4, 36u + m_dataBytes);
            put32(h + 24, kSpu2SampleRate);
            put32(h + 28, kSpu2SampleRate * 4u);
            put32(h + 40, m_dataBytes);
            std::fseek(m_file, 0, SEEK_SET);
            std::fwrite(h, 1, sizeof(h), m_file);
        }

        std::FILE *m_file = nullptr;
        uint32_t m_dataBytes = 0u;
    };
}

void Spu2AudioOutput::submit(const int16_t *samples, size_t frames)
{
    static Spu2WavCapture capture;
    capture.append(samples, frames);
    if (!m_impl->started && !m_impl->unavailable)
        m_impl->start();
    if (m_impl->started)
        m_impl->ring.push(samples, frames);
}
