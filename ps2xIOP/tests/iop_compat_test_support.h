#pragma once

#include "ps2x/iop/iop_subsystem.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace iop_test
{
    using namespace ps2x::iop;

    inline void require(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    class Host final : public IopHost
    {
    public:
        explicit Host(size_t bytes = 0x20000u) : guest(bytes, 0xCCu) {}

        bool readGuest(uint32_t address, void *destination, size_t size) const override
        {
            if ((size != 0u && !destination) || address > guest.size() || size > guest.size() - address)
                return false;
            if (size != 0u)
                std::memcpy(destination, guest.data() + address, size);
            ++guestReads;
            return true;
        }
        bool writeGuest(uint32_t address, const void *source, size_t size) override
        {
            if ((size != 0u && !source) || address > guest.size() || size > guest.size() - address)
                return false;
            if (size != 0u)
                std::memcpy(guest.data() + address, source, size);
            ++guestWrites;
            return true;
        }
        bool zeroGuest(uint32_t address, size_t size) override
        {
            if (address > guest.size() || size > guest.size() - address)
                return false;
            std::fill_n(guest.begin() + address, size, uint8_t{0});
            ++guestWrites;
            return true;
        }
        bool normalizeGuestAddress(uint32_t address, uint32_t &normalized) const override
        {
            normalized = address;
            return address < guest.size();
        }
        uint32_t allocateIopHandle(IopHandleKind) override { return nextHandle += 0x80u; }
        uint32_t allocateGuest(uint32_t, uint32_t) override { return 0u; }
        void freeGuest(uint32_t) override {}
        void audioCommand(uint32_t, uint32_t, GuestBuffer, GuestBuffer) override { ++audioCalls; }
        std::string hostPath(HostPathKind) const override { return {}; }
        std::string translateGuestPath(std::string_view path) const override { return std::string(path); }
        uint64_t openHostFile(std::string_view) override { return file.empty() ? 0u : 1u; }
        bool hostFileSize(uint64_t handle, uint64_t &size) const override
        {
            size = file.size();
            return handle == 1u && !file.empty();
        }
        bool readHostFile(uint64_t handle, uint64_t offset, void *destination, size_t size,
                          size_t &bytesRead) override
        {
            bytesRead = 0u;
            if (handle != 1u || offset > file.size())
                return false;
            bytesRead = std::min(size, file.size() - static_cast<size_t>(offset));
            if (bytesRead != 0u)
                std::memcpy(destination, file.data() + offset, bytesRead);
            return true;
        }
        void closeHostFile(uint64_t) override {}
        int32_t memoryCard(const MemoryCardRequest &request) override
        {
            cardCalls.push_back(request);
            return request.operation == MemoryCardOperation::Init ? initResult : 0;
        }
        bool hasGuestFunction(uint32_t) const override { return false; }
        bool invokeGuestFunction(uint64_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *) override
        {
            return false;
        }
        void log(LogLevel, std::string_view message) override { logs.emplace_back(message); }

        uint32_t word(uint32_t address) const
        {
            uint32_t value = 0u;
            require(readGuest(address, &value, sizeof(value)), "test read outside guest RAM");
            return value;
        }
        void fill(uint32_t address, size_t size, uint8_t value = 0xCCu)
        {
            require(address <= guest.size() && size <= guest.size() - address, "test fill outside RAM");
            std::fill_n(guest.begin() + address, size, value);
        }

        std::vector<uint8_t> guest;
        std::vector<uint8_t> file;
        std::vector<std::string> logs;
        std::vector<MemoryCardRequest> cardCalls;
        mutable size_t guestReads = 0u;
        size_t guestWrites = 0u;
        size_t audioCalls = 0u;
        int32_t initResult = 0;
        uint32_t nextHandle = 0x1000u;
    };

    inline RpcRequest request(uint32_t sid, uint32_t function, uint32_t size = 16u)
    {
        RpcRequest result{};
        result.sid = sid;
        result.function = function;
        result.receive = {0x800u, size};
        return result;
    }

    inline uint64_t metric(const IopSubsystem &iop, std::string_view service, std::string_view name)
    {
        for (const auto &row : iop.debugSnapshot().services)
            if (row.name == service)
                for (const auto &entry : row.metrics)
                    if (entry.name == name)
                        return entry.value;
        throw std::runtime_error("missing debug metric");
    }

    class Irx
    {
    public:
        explicit Irx(uint32_t base = 0x10000u, uint32_t imageBytes = 0x500u)
            : bytes(0x100u + imageBytes, 0u)
        {
            put32(0u, 0x464C457Fu);
            bytes[4] = bytes[5] = bytes[6] = 1u;
            put16(16u, 2u);
            put16(18u, 8u);
            put32(20u, 1u);
            put32(24u, base);
            put32(28u, 52u);
            put16(40u, 52u);
            put16(42u, 32u);
            put16(44u, 1u);
            put32(52u, 1u);
            put32(56u, 0x100u);
            put32(60u, base);
            put32(64u, base);
            put32(68u, imageBytes);
            put32(72u, imageBytes);
            put32(76u, 7u);
            put32(80u, 4u);
        }
        void words(uint32_t offset, std::initializer_list<uint32_t> values)
        {
            for (uint32_t value : values)
            {
                put32(0x100u + offset, value);
                offset += 4u;
            }
        }
        void install(Host &host, uint32_t address = 0x1000u) const
        {
            require(host.writeGuest(address, bytes.data(), bytes.size()), "synthetic IRX does not fit");
        }
        std::vector<uint8_t> bytes;

    private:
        void put16(uint32_t offset, uint16_t value)
        {
            require(offset + 2u <= bytes.size(), "IRX builder overflow");
            bytes[offset] = static_cast<uint8_t>(value);
            bytes[offset + 1u] = static_cast<uint8_t>(value >> 8u);
        }
        void put32(uint32_t offset, uint32_t value)
        {
            put16(offset, static_cast<uint16_t>(value));
            put16(offset + 2u, static_cast<uint16_t>(value >> 16u));
        }
    };

    inline Irx rpcServer(uint32_t sid, uint32_t reply)
    {
        Irx image;
        image.words(0u, {
            0x27BDFFE0u, 0xAFBF001Cu, // save ra
            0x3C040001u, 0x34840200u,
            0x3C050000u | (sid >> 16u), 0x34A50000u | (sid & 0xFFFFu),
            0x3C060001u, 0x34C60300u,
            0x3C070001u, 0x34E70400u,
            0xAFA00010u, 0xAFA00014u, 0xAFA00018u,
            0x0C00401Du, 0u, // jal 0x10074: sceSifRegisterRpc
            0x8FBF001Cu, 0x00001021u, 0x27BD0020u, 0x03E00008u, 0u,
        });
        image.words(0x60u, {0x41E00000u, 0u, 0x0101u, 0x63666973u, 0x0000646Du,
                             0x03E00008u, 0x24000011u, 0u, 0u});
        image.words(0x300u, {0x3C020001u, 0x34420400u, 0x03E00008u, 0u});
        image.words(0x400u, {reply, reply, reply, reply});
        return image;
    }

    struct Test
    {
        const char *name;
        void (*function)();
    };

    inline int run(std::span<const Test> tests)
    {
        size_t failures = 0u;
        for (const Test &test : tests)
        {
            try
            {
                test.function();
                std::cout << "PASS " << test.name << '\n';
            }
            catch (const std::exception &error)
            {
                ++failures;
                std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
            }
        }
        std::cout << tests.size() - failures << '/' << tests.size() << " cases passed\n";
        return failures == 0u ? 0 : 1;
    }
}
