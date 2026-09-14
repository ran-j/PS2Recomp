#include "ps2_vu1_capture.h"
#include "runtime/ps2_vu1.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <tuple>

namespace ps2_vu_detail {
void captureProgramStart(const char *directory, unsigned unit,
                         const uint8_t *code, uint32_t codeSize, uint64_t generation,
                         const uint8_t *data, uint32_t dataSize, const VU1State &state)
{
    static const char *trigger = std::getenv("PS2_VU_CAPTURE_TRIGGER");
    static const char *pcFilter = std::getenv("PS2_VU_CAPTURE_PC");
    if (pcFilter && state.pc != std::strtoul(pcFilter, nullptr, 0))
        return;
    static std::mutex mutex;
    const std::lock_guard lock(mutex);
    if (trigger && *trigger) {
        static bool triggered = false;
        static auto nextCheck = std::chrono::steady_clock::time_point::min();
        if (!triggered) {
            const auto now = std::chrono::steady_clock::now();
            if (now < nextCheck) return;
            nextCheck = now + std::chrono::milliseconds(250);
            std::error_code error;
            triggered = std::filesystem::exists(trigger, error);
            if (!triggered) return;
        }
    }
    static unsigned captured = 0;
    if (captured >= 256u)
        return;
    struct CodeHash {
        const uint8_t *code = nullptr;
        uint64_t generation = 0;
        uint64_t hash = 0;
    };
    static std::array<CodeHash, 2> hashes;
    auto &cached = hashes[unit];
    if (cached.code != code || cached.generation != generation) {
        cached = {code, generation, 14695981039346656037ull};
        for (uint32_t i = 0; i < codeSize; ++i)
            cached.hash = (cached.hash ^ code[i]) * 1099511628211ull;
    }
    static std::map<std::tuple<unsigned, uint32_t, uint64_t>, uint64_t> calls;
    const uint64_t call = ++calls[{unit, state.pc, cached.hash}];
    if ((call & (call - 1u)) != 0u)
        return;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        std::fprintf(stderr, "VU capture directory: %s\n", error.message().c_str());
        captured = 256u;
        return;
    }
    const std::string stem = std::string(directory) + "/vu" + std::to_string(unit) +
        "-pc" + std::to_string(state.pc) + "-call" + std::to_string(call) +
        "-code" + std::to_string(cached.hash);
    VU1State initial = state;
    initial.cycles = 0u;
    const auto write = [&](const char *suffix, const void *bytes, size_t size) {
        std::ofstream output(stem + suffix, std::ios::binary);
        output.write(static_cast<const char *>(bytes), static_cast<std::streamsize>(size));
        return static_cast<bool>(output);
    };
    if (!write(".code", code, codeSize) || !write(".data", data, dataSize) ||
        !write(".state", &initial, sizeof(initial))) {
        std::fprintf(stderr, "Unable to write VU capture %s\n", stem.c_str());
        captured = 256u;
        return;
    }
    ++captured;
}

}
