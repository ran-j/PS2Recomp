#include "ps2_audio.h"
#include "ps2_memory.h"
#include "raylib.h"
#include <cstring>
#include <vector>

namespace
{
std::vector<uint8_t> buildWavFromPcm(const int16_t *pcm, size_t sampleCount, uint32_t sampleRate)
{
    const uint32_t dataSize = static_cast<uint32_t>(sampleCount * 2);
    const uint32_t fileSize = 36 + dataSize;
    std::vector<uint8_t> wav(8 + fileSize);

    uint8_t *p = wav.data();
    p[0] = 'R'; p[1] = 'I'; p[2] = 'F'; p[3] = 'F';
    p[4] = static_cast<uint8_t>(fileSize);
    p[5] = static_cast<uint8_t>(fileSize >> 8);
    p[6] = static_cast<uint8_t>(fileSize >> 16);
    p[7] = static_cast<uint8_t>(fileSize >> 24);
    p[8] = 'W'; p[9] = 'A'; p[10] = 'V'; p[11] = 'E';
    p[12] = 'f'; p[13] = 'm'; p[14] = 't'; p[15] = ' ';
    p[16] = 16; p[17] = 0; p[18] = 0; p[19] = 0;
    p[20] = 1; p[21] = 0;
    p[22] = 1; p[23] = 0;
    p[24] = static_cast<uint8_t>(sampleRate);
    p[25] = static_cast<uint8_t>(sampleRate >> 8);
    p[26] = static_cast<uint8_t>(sampleRate >> 16);
    p[27] = static_cast<uint8_t>(sampleRate >> 24);
    const uint32_t byteRate = sampleRate * 2;
    p[28] = static_cast<uint8_t>(byteRate);
    p[29] = static_cast<uint8_t>(byteRate >> 8);
    p[30] = static_cast<uint8_t>(byteRate >> 16);
    p[31] = static_cast<uint8_t>(byteRate >> 24);
    p[32] = 2; p[33] = 0;
    p[34] = 16; p[35] = 0;
    p[36] = 'd'; p[37] = 'a'; p[38] = 't'; p[39] = 'a';
    p[40] = static_cast<uint8_t>(dataSize);
    p[41] = static_cast<uint8_t>(dataSize >> 8);
    p[42] = static_cast<uint8_t>(dataSize >> 16);
    p[43] = static_cast<uint8_t>(dataSize >> 24);
    std::memcpy(p + 44, pcm, dataSize);
    return wav;
}
}

namespace ps2_vag
{
bool decode(const uint8_t *data, uint32_t sizeBytes,
            std::vector<int16_t> &outPcm, uint32_t &outSampleRate);
}

struct PS2AudioBackend::Impl
{
    struct TrackedSound { Sound snd; uint32_t sampleKey; };
    std::vector<TrackedSound> activeSounds;
};

PS2AudioBackend::PS2AudioBackend() : m_impl(std::make_unique<Impl>())
{
}

PS2AudioBackend::~PS2AudioBackend()
{
    if (m_impl)
        stopAll();
}

void PS2AudioBackend::onVagTransfer(const uint8_t *rdram, uint32_t srcAddr, uint32_t sizeBytes)
{
    if (!rdram || sizeBytes < 48)
        return;

    const uint32_t physAddr = srcAddr & PS2_RAM_MASK;
    if (physAddr + sizeBytes > PS2_RAM_SIZE)
        return;

    std::vector<int16_t> pcm;
    uint32_t sampleRate = 44100;
    if (!ps2_vag::decode(rdram + physAddr, sizeBytes, pcm, sampleRate))
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    DecodedSample sample;
    sample.pcm = std::move(pcm);
    sample.sampleRate = sampleRate;
    m_sampleBank[physAddr] = std::move(sample);
    m_mostRecentSampleKey = physAddr;
}

void PS2AudioBackend::onVagTransferFromBuffer(const uint8_t *data, uint32_t sizeBytes, uint32_t keyAddr)
{
    if (!data || sizeBytes < 48)
        return;

    std::vector<int16_t> pcm;
    uint32_t sampleRate = 44100;
    if (!ps2_vag::decode(data, sizeBytes, pcm, sampleRate))
        return;

    const uint32_t physAddr = keyAddr & PS2_RAM_MASK;
    std::lock_guard<std::mutex> lock(m_mutex);
    DecodedSample sample;
    sample.pcm = std::move(pcm);
    sample.sampleRate = sampleRate;
    m_sampleBank[physAddr] = sample;
    m_mostRecentSampleKey = physAddr;
    m_loadOrderSamples.push_back(std::move(sample));
    constexpr size_t kMaxLoadOrderSamples = 32;
    if (m_loadOrderSamples.size() > kMaxLoadOrderSamples)
        m_loadOrderSamples.erase(m_loadOrderSamples.begin());
}

namespace
{
constexpr uint32_t LIBSD_CMD_SET_VOICE = 0x8010u;
}

void PS2AudioBackend::onSoundCommand(uint32_t sid, uint32_t rpcNum,
                                     const uint8_t *sendBuf, uint32_t sendSize,
                                     uint8_t *recvBuf, uint32_t recvSize)
{
    if (sid != 0x80000701u)
        return;

    if ((rpcNum == LIBSD_CMD_SET_VOICE || (rpcNum & 0xFF00u) == 0x8100u) &&
        sendBuf && sendSize >= 20)
    {
        uint32_t sampleAddr = 0;
        uint32_t voiceIndex = 0xFFFFFFFFu;
        for (int vo = 4; vo >= 0 && voiceIndex == 0xFFFFFFFFu; vo -= 4)
        {
            if (vo < static_cast<int>(sendSize))
            {
                uint32_t v = 0;
                std::memcpy(&v, sendBuf + vo, sizeof(v));
                if (v < 24u)
                    voiceIndex = v;
            }
        }

        constexpr uint32_t kMinPlausibleAddr = 0x1000u;
        for (int off = 12; off <= 24 && sampleAddr == 0; off += 4)
        {
            if (sendSize >= static_cast<uint32_t>(off + 4))
            {
                uint32_t cand = 0;
                std::memcpy(&cand, sendBuf + off, sizeof(cand));
                if (cand >= kMinPlausibleAddr && (cand <= PS2_RAM_MASK || (cand & ~PS2_RAM_MASK) == 0))
                    sampleAddr = cand;
            }
        }
        if (sampleAddr == 0)
            sampleAddr = m_mostRecentSampleKey;

        float pitch = 1.0f;
        if (sendSize >= 12)
        {
            uint16_t pitchHalf = 0;
            std::memcpy(&pitchHalf, sendBuf + 8, sizeof(pitchHalf));
            if (pitchHalf != 0)
                pitch = 4096.0f / static_cast<float>(pitchHalf);
        }
        play(sampleAddr, pitch, 1.0f, voiceIndex);
    }
}

void PS2AudioBackend::play(uint32_t sampleAddr, float pitch, float volume, uint32_t voiceIndex)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    DecodedSample *sampleToPlay = nullptr;
    uint32_t sampleKey = 0;

    auto it = m_sampleBank.find(sampleAddr & PS2_RAM_MASK);
    if (it != m_sampleBank.end())
    {
        sampleToPlay = &it->second;
        sampleKey = it->first;
    }
    else if (voiceIndex != 0xFFFFFFFFu && voiceIndex < m_loadOrderSamples.size())
    {
        sampleToPlay = &m_loadOrderSamples[voiceIndex];
        sampleKey = 0x1719740u + voiceIndex;
    }
    else
    {
        it = m_sampleBank.find(m_mostRecentSampleKey);
        if (it == m_sampleBank.end())
            return;
        sampleToPlay = &it->second;
        sampleKey = it->first;
    }
    if (!sampleToPlay || sampleToPlay->pcm.empty())
        return;

    const bool isBgm = (sampleToPlay->pcm.size() > static_cast<size_t>(sampleToPlay->sampleRate * 5));
    playDecodedSample(sampleKey, *sampleToPlay, pitch, volume, isBgm);
}

void PS2AudioBackend::pruneFinishedSounds()
{
    auto &sounds = m_impl->activeSounds;
    auto it = sounds.begin();
    while (it != sounds.end())
    {
        if (!IsSoundPlaying(it->snd))
        {
            UnloadSound(it->snd);
            it = sounds.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void PS2AudioBackend::playDecodedSample(uint32_t sampleKey, DecodedSample &sample, float pitch, float volume,
                                        bool isBgm)
{
    if (!m_audioReady || sample.pcm.empty())
        return;

    pruneFinishedSounds();

    for (const auto &t : m_impl->activeSounds)
    {
        if (t.sampleKey == sampleKey && IsSoundPlaying(t.snd))
            return;
    }

    auto &sounds = m_impl->activeSounds;
    if (isBgm)
    {
        for (auto it = sounds.begin(); it != sounds.end();)
        {
            if (IsSoundPlaying(it->snd))
            {
                StopSound(it->snd);
                UnloadSound(it->snd);
                it = sounds.erase(it);
            }
            else
                ++it;
        }
    }

    constexpr int kMaxConcurrentSounds = 4;
    while (static_cast<int>(sounds.size()) >= kMaxConcurrentSounds)
    {
        StopSound(sounds.front().snd);
        UnloadSound(sounds.front().snd);
        sounds.erase(sounds.begin());
    }

    std::vector<uint8_t> wav = buildWavFromPcm(sample.pcm.data(), sample.pcm.size(), sample.sampleRate);
    Wave wave = LoadWaveFromMemory(".wav", wav.data(), static_cast<int>(wav.size()));
    if (wave.frameCount <= 0)
        return;
    Sound snd = LoadSoundFromWave(wave);
    UnloadWave(wave);
    SetSoundPitch(snd, pitch);
    SetSoundVolume(snd, volume);
    m_impl->activeSounds.push_back({snd, sampleKey});
    PlaySound(snd);
}

void PS2AudioBackend::stop(uint32_t voiceId)
{
    (void)voiceId;
}

void PS2AudioBackend::stopAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &t : m_impl->activeSounds)
    {
        StopSound(t.snd);
        UnloadSound(t.snd);
    }
    m_impl->activeSounds.clear();
}
