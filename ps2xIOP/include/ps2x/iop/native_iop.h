#pragma once

#include "ps2x/iop/iop_host.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ps2x::iop
{
    // Runs a game's own IRX modules, sound drivers mostly, on an emulated IOP
    // with an SPU2. The audio callback calls render() while the EE thread loads
    // modules and makes calls, so every entry point takes the same lock.
    class NativeIop
    {
    public:
        explicit NativeIop(IopHost &host);
        ~NativeIop();

        NativeIop(const NativeIop &) = delete;
        NativeIop &operator=(const NativeIop &) = delete;

        // Module file names (for example "LIBSD.IRX") that run natively.
        // Once any are set, IOP memory handed to the EE is this IOP's.
        void setModules(std::vector<std::string> names);
        bool enabled() const;
        bool claims(std::string_view guestPath) const;
        // Load a claimed module; arguments are NUL-separated as sceSifLoadModule passes them.
        int32_t load(std::string_view guestPath, std::string_view arguments);

        // A server a native module registered, with its record and receive buffer.
        bool server(uint32_t sid, uint32_t &record, uint32_t &buffer) const;
        // Blocking SIF RPC. The reply is written to receiveAddress in EE memory.
        bool call(uint32_t sid, uint32_t function, uint32_t sendAddress, uint32_t sendSize,
                  uint32_t receiveAddress, uint32_t receiveSize);

        // IOP memory as the EE sees it through SIF DMA and the IOP heap calls.
        bool write(uint32_t address, const void *data, uint32_t size);
        bool read(uint32_t address, void *data, uint32_t size);
        uint32_t allocate(uint32_t size);
        void release(uint32_t address);

        // Fill interleaved 48 kHz stereo frames, running the IOP in step.
        void render(int16_t *frames, uint32_t count);
        bool active() const;

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
