#pragma once

#include "ps2x/iop/iop_host.h"

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <unordered_map>

class PS2Runtime;
struct R5900Context;

class PS2IopHostAdapter final : public ps2x::iop::IopHost
{
public:
    class CallScope
    {
    public:
        CallScope() = default;
        CallScope(PS2IopHostAdapter &owner, R5900Context *context, uint8_t *rdram);
        ~CallScope();

        CallScope(const CallScope &) = delete;
        CallScope &operator=(const CallScope &) = delete;
        CallScope(CallScope &&other) noexcept;
        CallScope &operator=(CallScope &&other) noexcept;

        [[nodiscard]] uint64_t token() const { return m_token; }

    private:
        void release();

        std::unique_lock<std::recursive_mutex> m_lock;
        PS2IopHostAdapter *m_owner = nullptr;
        R5900Context *m_previousContext = nullptr;
        uint8_t *m_previousRdram = nullptr;
        uint64_t m_previousToken = 0;
        uint64_t m_token = 0;
    };

    explicit PS2IopHostAdapter(PS2Runtime &runtime);
    ~PS2IopHostAdapter() override;

    [[nodiscard]] CallScope enterCall(R5900Context *context,
                                      uint8_t *rdram = nullptr);

    bool readGuest(uint32_t address, void *destination, size_t size) const override;
    bool writeGuest(uint32_t address, const void *source, size_t size) override;
    bool zeroGuest(uint32_t address, size_t size) override;
    bool normalizeGuestAddress(uint32_t address, uint32_t &normalized) const override;
    uint32_t allocateIopHandle(ps2x::iop::IopHandleKind kind) override;
    uint32_t allocateGuest(uint32_t size, uint32_t alignment) override;
    void freeGuest(uint32_t address) override;

    void audioCommand(uint32_t sid,
                      uint32_t function,
                      ps2x::iop::GuestBuffer send,
                      ps2x::iop::GuestBuffer receive) override;

    std::string hostPath(ps2x::iop::HostPathKind kind) const override;
    std::string translateGuestPath(std::string_view path) const override;
    uint64_t openHostFile(std::string_view path) override;
    bool hostFileSize(uint64_t handle, uint64_t &size) const override;
    bool readHostFile(uint64_t handle,
                      uint64_t offset,
                      void *destination,
                      size_t size,
                      size_t &bytesRead) override;
    void closeHostFile(uint64_t handle) override;

    int32_t memoryCard(const ps2x::iop::MemoryCardRequest &request) override;

    bool hasGuestFunction(uint32_t address) const override;
    bool invokeGuestFunction(uint64_t callToken,
                             uint32_t address,
                             uint32_t a0,
                             uint32_t a1,
                             uint32_t a2,
                             uint32_t a3,
                             uint32_t *resultAddress) override;

    void log(ps2x::iop::LogLevel level, std::string_view message) override;

private:
    friend class CallScope;

    bool guestRange(uint32_t address, size_t size, uint8_t *&begin) const;

    PS2Runtime &m_runtime;
    std::recursive_mutex m_callMutex;
    R5900Context *m_activeContext = nullptr;
    uint8_t *m_activeRdram = nullptr;
    uint64_t m_activeToken = 0;
    uint64_t m_nextToken = 1;
    struct HostFile
    {
        std::FILE *stream = nullptr;
        uint64_t size = 0u;
    };
    mutable std::mutex m_hostFileMutex;
    std::unordered_map<uint64_t, HostFile> m_hostFiles;
    uint64_t m_nextHostFileHandle = 1u;
};
