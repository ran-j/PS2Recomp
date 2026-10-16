#pragma once
#include <cstdint>

constexpr uint32_t kThreadReturnSentinel = 0x1FFFFF00u;
constexpr uint32_t kCallReturnSentinel = 0x1FFFFF04u;
// Return address of an RPC server function running on the thread parked in
// its sceSifRpcLoop (IopEmulator::Impl::executeRpcServer).
constexpr uint32_t kRpcReturnSentinel = 0x1FFFFF08u;
constexpr uint64_t kIopClockHz = 36'864'000ull;
// An RPC server that has not returned after this much IOP time (two seconds)
// is reported as stuck and left running on its thread.
constexpr uint64_t kRpcServerCycleLimit = kIopClockHz * 2ull;
// Scheduler quantum used while the EE waits for an RPC server to return.
constexpr uint64_t kRpcServerDriveCycles = 2048u;
// NTSC field cadence (approximately 59.94 Hz).  VBlank imports are
// scheduler waits, not no-op timing hints: returning immediately lets
// high-priority IRX threads busy-loop and starve RPC server threads.
constexpr uint64_t kVblankPeriodCycles = (kIopClockHz * 1001ull + 30'000ull) / 60'000ull;
constexpr uint64_t kVblankEndPhaseCycles = kVblankPeriodCycles / 16ull;
constexpr uint32_t kDefaultSlice = 256u;
constexpr uint32_t kMaxCallInstructions = 2'000'000u;
constexpr uint32_t kModuleLoadBase = 0x00010000u;
constexpr uint32_t kStackGuardBytes = 64u;