#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ps2x::iop
{
    struct GuestBuffer
    {
        uint32_t address = 0;
        uint32_t size = 0;
    };

    struct GameIdentity
    {
        std::string elfName;
        uint32_t entryPoint = 0;
        uint32_t crc32 = 0;
    };

    struct GameMatcher
    {
        std::string elfName;
        uint32_t entryPoint = 0;
        uint32_t crc32 = 0;
    };

    enum class RpcAbi : uint32_t
    {
        RuntimeDefault = 0,
        Registers = 1,
        Stack = 2,
    };

    struct RpcCallCandidate
    {
        uint32_t sendSize = 0;
        uint32_t receiveAddress = 0;
        uint32_t receiveSize = 0;
        uint32_t endFunction = 0;
        uint32_t endParameter = 0;
        bool plausible = false;
    };

    struct RpcAbiRequest
    {
        uint32_t boundSid = 0;
        uint32_t function = 0;
        RpcCallCandidate registers;
        RpcCallCandidate stack;
    };

    struct RpcRequest
    {
        uint64_t callToken = 0;
        uint32_t clientAddress = 0;
        uint32_t serverAddress = 0;
        uint32_t serverFunction = 0;
        uint32_t serverBuffer = 0;
        uint32_t sid = 0;
        uint32_t function = 0;
        uint32_t mode = 0;
        GuestBuffer send;
        GuestBuffer receive;
        uint32_t endFunction = 0;
        uint32_t endParameter = 0;
    };

    enum class IopHandleKind : uint32_t
    {
        RpcServer = 0,
        RpcPacket = 1,
    };

    enum class CallbackPolicy : uint32_t
    {
        RuntimeDefault = 0,
        Suppress = 1,
    };

    enum class ServerDispatchPolicy : uint32_t
    {
        RuntimeDefault = 0,
        Suppress = 1,
    };

    struct RpcResult
    {
        bool handled = false;
        uint32_t resultAddress = 0;
        bool signalNowaitCompletion = false;
        bool signalCompletion = false;
        CallbackPolicy callbackPolicy = CallbackPolicy::RuntimeDefault;
        ServerDispatchPolicy serverDispatchPolicy = ServerDispatchPolicy::RuntimeDefault;
    };

    enum class SifTransferKind : uint32_t
    {
        SetDma = 0,
        GetOtherData = 1,
    };

    enum class SifTransferPhase : uint32_t
    {
        BeforeCopy = 0,
        AfterCopy = 1,
    };

    struct SifTransfer
    {
        SifTransferKind kind = SifTransferKind::SetDma;
        SifTransferPhase phase = SifTransferPhase::AfterCopy;
        uint32_t sourceAddress = 0;
        uint32_t destinationAddress = 0;
        uint32_t size = 0;
    };

    struct DebugMetric
    {
        std::string name;
        uint64_t value = 0;
        bool hexadecimal = false;
    };

    struct DebugService
    {
        std::string name;
        std::vector<uint32_t> sids;
        bool profileSpecific = false;
        std::vector<DebugMetric> metrics;
    };

    struct DebugSnapshot
    {
        std::string activeProfile;
        std::string activeProvider;
        std::vector<DebugService> services;
        std::vector<std::string> diagnostics;
    };
}
