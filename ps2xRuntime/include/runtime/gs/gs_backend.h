#pragma once

#include <cstdint>
#include <memory>

class GSBackend
{
public:
    virtual ~GSBackend() = default;

    virtual bool Initialize(
        uint8_t* vram,
        uint32_t vramSize
    ) = 0;

    virtual void Reset() = 0;

    virtual void Flush() = 0;

    virtual void Sync() = 0;

    virtual bool IsAvailable() const = 0;
};
