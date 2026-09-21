#include "runtime/ps2_vu1.h"

#include <algorithm>
#include <cstring>

void VU1Interpreter::rebuildDecodedCodeCache(const uint8_t *vuCode, uint32_t codeSize,
                                           const PS2Memory *memory, uint64_t generation)
{
    const bool reusable = m_decodedCodeCacheValid && m_cachedVuCode == vuCode &&
        m_cachedMemory == memory && m_cachedCodeSize == codeSize;
    const uint32_t pairCount = std::min<uint32_t>(codeSize / 8u, kMaxDecodedPairs);
    uint32_t changedWindow = 0u;
    for (uint32_t i = pairCount; i-- > 0u;)
    {
        uint32_t words[2];
        std::memcpy(words, vuCode + i * 8u, sizeof(words));
        const auto &old = m_decodedCodeCache[i];
        const bool changed = !reusable || old.lower != words[0] || old.upper != words[1];
        // A later edit must invalidate every native region that can contain it.
        // The registry supports regions of up to thirty-two pairs without code wrap.
        changedWindow = (changedWindow << 1u) | static_cast<uint32_t>(changed);
        if (changed)
            m_decodedCodeCache[i] = decodeInstructionPair(vuCode, i * 8u);
        if (changedWindow != 0u)
            m_compiledCodeCache[i] = findCompiledBlock(vuCode + i * 8u, codeSize - i * 8u, m_unit);
    }
    m_cachedVuCode = vuCode;
    m_cachedMemory = memory;
    m_cachedCodeSize = codeSize;
    m_cachedCodeGeneration = generation;
    m_decodedCodeCacheValid = true;
}
