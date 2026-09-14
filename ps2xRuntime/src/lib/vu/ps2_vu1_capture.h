#pragma once

#include <cstdint>
struct VU1State;

namespace ps2_vu_detail {
void captureProgramStart(const char *directory, unsigned unit,
                         const uint8_t *code, uint32_t codeSize, uint64_t generation,
                         const uint8_t *data, uint32_t dataSize, const VU1State &state);
}
