#include "runtime/ps2_guest_code_image.h"

#include <cstring>
#include <utility>

void GuestCodeImage::reset()
{
    m_segments.clear();
    m_reported.clear();
}

void GuestCodeImage::addSegment(uint32_t guestBase, uint32_t physicalBase, const uint8_t *bytes, uint32_t size)
{
    Segment segment;
    segment.guestBase = guestBase;
    segment.physicalBase = physicalBase;
    segment.words.resize(size / 4u);
    std::memcpy(segment.words.data(), bytes, segment.words.size() * sizeof(uint32_t));
    m_segments.push_back(std::move(segment));
}

std::vector<GuestCodeImage::Modification> GuestCodeImage::modifiedWords(const uint8_t *rdram) const
{
    std::vector<Modification> modified;
    if (!rdram)
    {
        return modified;
    }
    for (const Segment &segment : m_segments)
    {
        const uint8_t *current = rdram + segment.physicalBase;
        if (std::memcmp(current, segment.words.data(), segment.words.size() * sizeof(uint32_t)) == 0)
        {
            continue;
        }
        for (size_t i = 0; i < segment.words.size(); ++i)
        {
            uint32_t word = 0u;
            std::memcpy(&word, current + i * sizeof(uint32_t), sizeof(word));
            if (word != segment.words[i])
            {
                modified.push_back({segment.guestBase + static_cast<uint32_t>(i * sizeof(uint32_t)),
                                    segment.words[i],
                                    word});
            }
        }
    }
    return modified;
}

bool GuestCodeImage::firstReport(uint32_t address, uint32_t word)
{
    return m_reported.insert((static_cast<uint64_t>(address) << 32) | word).second;
}
