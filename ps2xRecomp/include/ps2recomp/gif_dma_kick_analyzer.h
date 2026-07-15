#ifndef PS2RECOMP_GIF_DMA_KICK_ANALYZER_H
#define PS2RECOMP_GIF_DMA_KICK_ANALYZER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "ps2recomp/types.h"

namespace ps2recomp
{
    struct Instruction;

    struct ConstantRegisterState
    {
        std::array<bool, 32> known{};
        std::array<uint32_t, 32> values{};

        ConstantRegisterState();
        void clear();
        bool read(uint32_t reg, uint32_t &value) const;
        void write(uint32_t reg, uint32_t value);
        void invalidate(uint32_t reg);
    };

    struct GifDmaKickPlan
    {
        bool valid = false;
        std::array<size_t, 4> storeIndices{};
        std::array<std::string, 4> values{};
        std::array<std::string, 4> captureExpressions{};
        std::array<bool, 4> captures{};
        size_t branchIndex = std::numeric_limits<size_t>::max();
        size_t endIndex = 0;
        bool completesInDelaySlot = false;

        bool suppresses(size_t index) const;
        bool completesAt(size_t index) const;
        size_t slotFor(size_t index) const;
    };

    bool isDirectMemoryAccess(const Instruction &inst);
    MemoryAccessHint resolveMemoryAccessHint(const Instruction &inst, const ConstantRegisterState &constants);
    void updateConstantRegisters(const Instruction &inst, ConstantRegisterState &constants);

    GifDmaKickPlan tryBuildGifDmaKickPlan(const std::vector<Instruction> &instructions,
                                          size_t startIndex,
                                          const ConstantRegisterState &constants,
                                          const std::unordered_set<uint32_t> &internalTargets);

    std::string gifDmaKickCall(const GifDmaKickPlan &plan);
    void emitGifDmaCapture(std::ostream &out, const GifDmaKickPlan &plan, size_t slot, std::string_view indent);
    std::string gifDmaDelaySlotOverride(const Instruction &delaySlot, const GifDmaKickPlan &plan, bool emitComments);
}

#endif // PS2RECOMP_GIF_DMA_KICK_ANALYZER_H
