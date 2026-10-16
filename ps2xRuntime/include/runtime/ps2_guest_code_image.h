#ifndef PS2_GUEST_CODE_IMAGE_H
#define PS2_GUEST_CODE_IMAGE_H

#include <cstdint>
#include <unordered_set>
#include <vector>

// The recompiled code words as the ELF shipped them. Recompiled code executes
// the instructions it was generated from, so a word the guest rewrites at run
// time takes effect only where the runtime decides the transfer: the direct
// J/JAL sites that dispatch through PS2Runtime::dispatchGuestBranch. This
// record tells the runtime which words the guest has rewritten. Used only
// from the EE executor thread.
class GuestCodeImage
{
public:
    struct Modification
    {
        uint32_t address = 0;
        uint32_t elfWord = 0;
        uint32_t currentWord = 0;
    };

    void reset();

    // Records size bytes of code that the guest runs at guestBase and that
    // sit in RDRAM at physicalBase.
    void addSegment(uint32_t guestBase, uint32_t physicalBase, const uint8_t *bytes, uint32_t size);

    // Every recorded word whose value in rdram differs from the ELF's.
    [[nodiscard]] std::vector<Modification> modifiedWords(const uint8_t *rdram) const;

    // True the first time it is called for this (address, word) pair.
    bool firstReport(uint32_t address, uint32_t word);

private:
    struct Segment
    {
        uint32_t guestBase = 0;
        uint32_t physicalBase = 0;
        std::vector<uint32_t> words;
    };

    std::vector<Segment> m_segments;
    std::unordered_set<uint64_t> m_reported;
};

#endif // PS2_GUEST_CODE_IMAGE_H
