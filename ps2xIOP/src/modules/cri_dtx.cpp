#include "../module_factories.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kDtxHeaderSize = 16u;
        constexpr uint32_t kDtxCommandSize = 16u;
        constexpr uint32_t kMaxDtxCommands = 128u;
        constexpr uint32_t kMinimumDtxWorkSize = 64u;
        constexpr uint32_t kDefaultSjrmtCapacity = 0x4000u;
        constexpr uint32_t kMaximumSjrmtCapacity = 0x01000000u;

        template <typename T>
        bool readGuestPod(const IopHost &host, uint32_t address, T &value)
        {
            value = {};
            return host.readGuest(address, &value, sizeof(value));
        }

        template <typename T>
        bool writeGuestPod(IopHost &host, uint32_t address, const T &value)
        {
            return host.writeGuest(address, &value, sizeof(value));
        }

        uint32_t normalizeSjrmtCapacity(uint32_t requestedBytes)
        {
            if (requestedBytes == 0u || requestedBytes > kMaximumSjrmtCapacity)
            {
                return kDefaultSjrmtCapacity;
            }
            return requestedBytes;
        }

        bool looksLikeCreate34Candidate(const RpcCallCandidate &candidate)
        {
            return candidate.receiveAddress != 0u &&
                   candidate.receiveSize >= sizeof(uint32_t) &&
                   candidate.receiveSize <= 0x40u &&
                   candidate.sendSize >= 12u &&
                   candidate.sendSize <= 0x1000u;
        }

        class CriDtxService final : public IopService
        {
        public:
            CriDtxService(IopHost &host, CriDtxBindings bindings)
                : m_host(host),
                  m_bindings(std::move(bindings)),
                  m_sids{m_bindings.sid}
            {
                reset();
            }

            [[nodiscard]] std::string_view name() const override
            {
                return m_bindings.serviceName;
            }

            [[nodiscard]] std::span<const uint32_t> sids() const override
            {
                return m_sids;
            }

            void reset() override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_remoteById.clear();
                m_transferById.clear();
                m_sjxByHandle.clear();
                m_ps2RnaByHandle.clear();
                m_sjrmtByHandle.clear();
                m_nextUrpcObject = m_bindings.urpcObjectBase;
                m_dmaAcks = 0u;
                m_dmaMisses = 0u;
                m_urpcCalls = 0u;
            }

            [[nodiscard]] RpcAbi selectRpcAbi(const RpcAbiRequest &request) const override
            {
                if (request.boundSid == m_bindings.sid &&
                    request.function == 0x422u &&
                    request.stack.plausible &&
                    looksLikeCreate34Candidate(request.stack) &&
                    !looksLikeCreate34Candidate(request.registers))
                {
                    return RpcAbi::Stack;
                }
                return RpcAbi::RuntimeDefault;
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                RpcResult result{};
                if (request.sid != m_bindings.sid)
                {
                    return result;
                }

                // DTX owns this SID. Its base protocol intentionally bypasses generic EE
                // server dispatch, and URPC dispatch is gated by the guest function table.
                result.serverDispatchPolicy = ServerDispatchPolicy::Suppress;

                const bool isUrpc = request.function >= 0x400u && request.function < 0x500u;
                const uint32_t command = isUrpc ? (request.function & 0xFFu) : 0u;
                uint32_t urpcFunction = 0u;
                uint32_t urpcObject = 0u;
                if (isUrpc && command < 64u)
                {
                    (void)readGuestPod(m_host,
                                       m_bindings.urpcFunctionTableBase + (command * sizeof(uint32_t)),
                                       urpcFunction);
                    (void)readGuestPod(m_host,
                                       m_bindings.urpcObjectTableBase + (command * sizeof(uint32_t)),
                                       urpcObject);
                }
                (void)urpcObject;

                const bool hasUrpcHandler = isUrpc && command < 64u && urpcFunction != 0u;
                if (hasUrpcHandler && request.serverFunction != 0u)
                {
                    uint32_t serverResult = 0u;
                    if (m_host.invokeGuestFunction(request.callToken,
                                                   request.serverFunction,
                                                   request.function,
                                                   request.serverBuffer,
                                                   request.send.size,
                                                   0u,
                                                   &serverResult))
                    {
                        result.handled = true;
                        result.resultAddress = serverResult;
                        if (result.resultAddress == 0u && request.serverBuffer != 0u)
                        {
                            result.resultAddress = request.serverBuffer;
                        }
                        if (result.resultAddress == 0u && request.receive.address != 0u)
                        {
                            result.resultAddress = request.receive.address;
                        }
                        return result;
                    }
                }

                if (hasUrpcHandler &&
                    request.send.address != 0u &&
                    request.send.size > 0u)
                {
                    uint32_t dispatcherResult = 0u;
                    if (m_host.invokeGuestFunction(request.callToken,
                                                   m_bindings.dispatcherFunctionAddress,
                                                   request.function,
                                                   request.send.address,
                                                   request.send.size,
                                                   0u,
                                                   &dispatcherResult))
                    {
                        result.handled = true;
                        result.resultAddress = dispatcherResult != 0u
                                                   ? dispatcherResult
                                                   : request.send.address;
                        return result;
                    }
                }

                if (request.function == 2u &&
                    request.receive.address != 0u &&
                    request.receive.size >= sizeof(uint32_t))
                {
                    return handleCreateTransport(request, result);
                }
                if (request.function == 3u)
                {
                    return handleDestroyTransport(request, result);
                }
                if (isUrpc)
                {
                    return emulateUrpc(request, command, result);
                }

                // Unknown calls remain visibly unhandled, but must never escape to a
                // generic registered server for this configured SID.
                return result;
            }

            void onSifTransfer(const SifTransfer &transfer) override
            {
                if (transfer.kind != SifTransferKind::SetDma ||
                    transfer.phase != SifTransferPhase::AfterCopy ||
                    transfer.size < kMinimumDtxWorkSize)
                {
                    return;
                }

                uint32_t normalizedSource = 0u;
                uint32_t normalizedDestination = 0u;
                if (!m_host.normalizeGuestAddress(transfer.sourceAddress, normalizedSource) ||
                    !m_host.normalizeGuestAddress(transfer.destinationAddress, normalizedDestination))
                {
                    return;
                }

                TransferState matched{};
                bool found = false;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    for (const auto &[id, state] : m_transferById)
                    {
                        (void)id;
                        if (matchesTransfer(state,
                                            normalizedSource,
                                            normalizedDestination,
                                            transfer.size))
                        {
                            matched = state;
                            found = true;
                            break;
                        }
                    }

                    if (!found)
                    {
                        found = inferTransferFromPayloadLocked(normalizedSource,
                                                               normalizedDestination,
                                                               transfer.size,
                                                               matched);
                    }

                    if (!found)
                    {
                        ++m_dmaMisses;
                    }
                }

                if (!found)
                {
                    return;
                }

                const uint32_t footerAddress =
                    matched.eeWorkAddress + matched.workSize - sizeof(uint32_t);
                uint32_t ticket = 0u;
                if (!readGuestPod(m_host, footerAddress, ticket))
                {
                    return;
                }
                (void)writeGuestPod(m_host, footerAddress, ticket + 1u);

                if (matched.dtxId == 0u)
                {
                    applySjxPayload(matched);
                }
                else if (matched.dtxId == 1u)
                {
                    applyPs2RnaPayload(matched);
                }

                std::lock_guard<std::mutex> lock(m_mutex);
                ++m_dmaAcks;
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                metrics.push_back({"sid", m_bindings.sid, true});
                metrics.push_back({"remote_handles", m_remoteById.size(), false});
                metrics.push_back({"transfers", m_transferById.size(), false});
                metrics.push_back({"sjx_objects", m_sjxByHandle.size(), false});
                metrics.push_back({"ps2rna_objects", m_ps2RnaByHandle.size(), false});
                metrics.push_back({"sjrmt_objects", m_sjrmtByHandle.size(), false});
                metrics.push_back({"next_urpc_object", m_nextUrpcObject, true});
                metrics.push_back({"urpc_calls", m_urpcCalls, false});
                metrics.push_back({"dma_acks", m_dmaAcks, false});
                metrics.push_back({"dma_misses", m_dmaMisses, false});
            }

        private:
            struct TransferState
            {
                uint32_t dtxId = 0u;
                uint32_t remoteHandle = 0u;
                uint32_t eeWorkAddress = 0u;
                uint32_t iopWorkAddress = 0u;
                uint32_t workSize = 0u;
            };

            struct SjxState
            {
                uint32_t handle = 0u;
                uint32_t sourceSjHandle = 0u;
                uint32_t destinationSjHandle = 0u;
                uint32_t line = 0u;
                uint32_t eeObjectAddress = 0u;
                uint16_t xid = 0u;
            };

            struct Ps2RnaState
            {
                uint32_t handle = 0u;
                uint32_t maxChannels = 0u;
                uint32_t sjHandle0 = 0u;
                uint32_t sjHandle1 = 0u;
                uint32_t channelCount = 0u;
                uint32_t sampleFrequency = 0u;
                uint32_t volume = 0u;
                bool playEnabled = false;
            };

            struct SjrmtState
            {
                uint32_t handle = 0u;
                uint32_t mode = 0u;
                uint32_t workAddress = 0u;
                uint32_t workSize = 0u;
                uint32_t readPosition = 0u;
                uint32_t writePosition = 0u;
                uint32_t roomBytes = 0u;
                uint32_t dataBytes = 0u;
                uint32_t uuid0 = 0u;
                uint32_t uuid1 = 0u;
                uint32_t uuid2 = 0u;
                uint32_t uuid3 = 0u;
            };

            static bool matchesTransfer(const TransferState &state,
                                        uint32_t sourceAddress,
                                        uint32_t destinationAddress,
                                        uint32_t size)
            {
                (void)destinationAddress;
                return state.eeWorkAddress != 0u &&
                       state.workSize >= kMinimumDtxWorkSize &&
                       state.eeWorkAddress == sourceAddress &&
                       state.workSize == size;
            }

            uint32_t normalizeAddress(uint32_t address) const
            {
                uint32_t normalized = 0u;
                (void)m_host.normalizeGuestAddress(address, normalized);
                return normalized;
            }

            RpcResult handleCreateTransport(const RpcRequest &request, RpcResult result)
            {
                uint32_t dtxId = 0u;
                uint32_t eeWorkAddress = 0u;
                uint32_t iopWorkAddress = 0u;
                uint32_t workSize = 0u;
                if (request.send.address != 0u && request.send.size >= sizeof(uint32_t))
                {
                    (void)readGuestPod(m_host, request.send.address, dtxId);
                }
                if (request.send.address != 0u && request.send.size >= 4u * sizeof(uint32_t))
                {
                    (void)readGuestPod(m_host, request.send.address + 4u, eeWorkAddress);
                    (void)readGuestPod(m_host, request.send.address + 8u, iopWorkAddress);
                    (void)readGuestPod(m_host, request.send.address + 12u, workSize);
                }

                const uint32_t normalizedEeWorkAddress = normalizeAddress(eeWorkAddress);
                const uint32_t normalizedIopWorkAddress = normalizeAddress(iopWorkAddress);

                uint32_t remoteHandle = 0u;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    const auto existing = m_remoteById.find(dtxId);
                    if (existing != m_remoteById.end())
                    {
                        remoteHandle = existing->second;
                    }

                    if (remoteHandle == 0u)
                    {
                        remoteHandle = m_host.allocateIopHandle(IopHandleKind::RpcServer);
                        if (remoteHandle == 0u)
                        {
                            remoteHandle = m_host.allocateIopHandle(IopHandleKind::RpcPacket);
                        }
                        if (remoteHandle == 0u)
                        {
                            remoteHandle = m_bindings.rpcServerPoolBase +
                                           ((dtxId & 0xFFu) * m_bindings.rpcServerStride);
                        }
                        m_remoteById[dtxId] = remoteHandle;
                    }

                    m_transferById[dtxId] = TransferState{
                        dtxId,
                        remoteHandle,
                        normalizedEeWorkAddress,
                        normalizedIopWorkAddress,
                        workSize,
                    };
                }

                (void)writeGuestPod(m_host, request.receive.address, remoteHandle);
                if (request.receive.size > sizeof(uint32_t))
                {
                    (void)m_host.zeroGuest(request.receive.address + sizeof(uint32_t),
                                           request.receive.size - sizeof(uint32_t));
                }

                result.handled = true;
                result.resultAddress = request.receive.address;
                return result;
            }

            RpcResult handleDestroyTransport(const RpcRequest &request, RpcResult result)
            {
                uint32_t remoteHandle = 0u;
                if (request.send.address != 0u &&
                    request.send.size >= sizeof(uint32_t) &&
                    readGuestPod(m_host, request.send.address, remoteHandle) &&
                    remoteHandle != 0u)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    for (auto it = m_remoteById.begin(); it != m_remoteById.end(); ++it)
                    {
                        if (it->second == remoteHandle)
                        {
                            m_transferById.erase(it->first);
                            m_remoteById.erase(it);
                            break;
                        }
                    }
                }

                if (request.receive.address != 0u && request.receive.size > 0u)
                {
                    (void)m_host.zeroGuest(request.receive.address, request.receive.size);
                }
                result.handled = true;
                result.resultAddress = request.receive.address;
                return result;
            }

            uint32_t allocateUrpcHandleLocked()
            {
                if (m_nextUrpcObject < m_bindings.urpcObjectBase ||
                    m_nextUrpcObject >= m_bindings.urpcObjectLimit)
                {
                    m_nextUrpcObject = m_bindings.urpcObjectBase;
                }

                for (uint32_t attempt = 0u; attempt < 4096u; ++attempt)
                {
                    const uint32_t candidate = m_nextUrpcObject;
                    m_nextUrpcObject += m_bindings.urpcObjectStride;
                    if (m_nextUrpcObject < m_bindings.urpcObjectBase ||
                        m_nextUrpcObject >= m_bindings.urpcObjectLimit)
                    {
                        m_nextUrpcObject = m_bindings.urpcObjectBase;
                    }

                    if (candidate < m_bindings.urpcObjectBase ||
                        candidate >= m_bindings.urpcObjectLimit ||
                        m_sjrmtByHandle.find(candidate) != m_sjrmtByHandle.end() ||
                        m_sjxByHandle.find(candidate) != m_sjxByHandle.end() ||
                        m_ps2RnaByHandle.find(candidate) != m_ps2RnaByHandle.end())
                    {
                        continue;
                    }

                    const bool usedByRemote =
                        std::any_of(m_remoteById.begin(), m_remoteById.end(),
                                    [candidate](const auto &entry)
                                    { return entry.second == candidate; });
                    if (!usedByRemote)
                    {
                        return candidate;
                    }
                }

                return m_bindings.urpcObjectBase;
            }

            RpcResult emulateUrpc(const RpcRequest &request,
                                  uint32_t command,
                                  RpcResult result)
            {
                std::array<uint32_t, 4> output = {1u, 0u, 0u, 0u};
                uint32_t outputWordCount = 1u;

                auto readSendWord = [&](uint32_t index, uint32_t &value)
                {
                    const uint64_t byteOffset =
                        static_cast<uint64_t>(index) * sizeof(uint32_t);
                    if (request.send.address == 0u ||
                        request.send.size < byteOffset + sizeof(uint32_t))
                    {
                        value = 0u;
                        return false;
                    }
                    return readGuestPod(m_host,
                                        request.send.address + static_cast<uint32_t>(byteOffset),
                                        value);
                };

                switch (command)
                {
                case 0u: // SJX_CREATE
                {
                    uint32_t sourceSjHandle = 0u;
                    uint32_t destinationSjHandle = 0u;
                    uint32_t line = 0u;
                    uint32_t eeObjectAddress = 0u;
                    (void)readSendWord(0u, sourceSjHandle);
                    (void)readSendWord(1u, destinationSjHandle);
                    (void)readSendWord(2u, line);
                    (void)readSendWord(3u, eeObjectAddress);

                    std::lock_guard<std::mutex> lock(m_mutex);
                    const uint32_t handle = allocateUrpcHandleLocked();
                    m_sjxByHandle[handle] = SjxState{
                        handle,
                        sourceSjHandle,
                        destinationSjHandle,
                        line,
                        eeObjectAddress,
                        0u,
                    };
                    output[0] = handle != 0u ? handle : 1u;
                    break;
                }
                case 1u: // SJX_DESTROY
                {
                    uint32_t handle = 0u;
                    (void)readSendWord(0u, handle);
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_sjxByHandle.erase(handle);
                    output[0] = 1u;
                    break;
                }
                case 2u: // SJX_RESET
                {
                    uint32_t handle = 0u;
                    uint32_t xid = 0u;
                    (void)readSendWord(0u, handle);
                    (void)readSendWord(1u, xid);
                    std::lock_guard<std::mutex> lock(m_mutex);
                    const auto it = m_sjxByHandle.find(handle);
                    if (it != m_sjxByHandle.end())
                    {
                        it->second.xid = static_cast<uint16_t>(xid & 0xFFFFu);
                    }
                    output[0] = 1u;
                    break;
                }
                case 8u: // PS2RNA_CREATE
                {
                    uint32_t maxChannels = 0u;
                    uint32_t sjHandle0 = 0u;
                    uint32_t sjHandle1 = 0u;
                    (void)readSendWord(0u, maxChannels);
                    (void)readSendWord(2u, sjHandle0);
                    (void)readSendWord(3u, sjHandle1);

                    std::lock_guard<std::mutex> lock(m_mutex);
                    const uint32_t handle = allocateUrpcHandleLocked();
                    m_ps2RnaByHandle[handle] = Ps2RnaState{
                        handle,
                        maxChannels,
                        sjHandle0,
                        sjHandle1,
                        maxChannels,
                        0u,
                        0u,
                        false,
                    };
                    output[0] = handle != 0u ? handle : 1u;
                    break;
                }
                case 9u: // PS2RNA_DESTROY
                {
                    uint32_t handle = 0u;
                    (void)readSendWord(0u, handle);
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_ps2RnaByHandle.erase(handle);
                    output[0] = 1u;
                    break;
                }
                case 32u: // SJRMT_RBF_CREATE
                case 33u: // SJRMT_MEM_CREATE
                case 34u: // SJRMT_UNI_CREATE
                {
                    uint32_t argument0 = 0u;
                    uint32_t argument1 = 0u;
                    uint32_t argument2 = 0u;
                    (void)readSendWord(0u, argument0);
                    (void)readSendWord(1u, argument1);
                    (void)readSendWord(2u, argument2);

                    uint32_t mode = 0u;
                    uint32_t workAddress = 0u;
                    uint32_t workSize = 0u;
                    if (command == 34u)
                    {
                        mode = argument0;
                        workAddress = argument1;
                        workSize = argument2;
                    }
                    else if (command == 33u)
                    {
                        workAddress = argument0;
                        workSize = argument1;
                    }
                    else
                    {
                        workAddress = argument0;
                        workSize = argument1 != 0u ? argument1 : argument2;
                    }
                    workSize = normalizeSjrmtCapacity(workSize);

                    std::lock_guard<std::mutex> lock(m_mutex);
                    const uint32_t handle = allocateUrpcHandleLocked();
                    m_sjrmtByHandle[handle] = SjrmtState{
                        handle,
                        mode,
                        workAddress,
                        workSize,
                        0u,
                        0u,
                        workSize,
                        0u,
                        0x53524D54u,
                        handle,
                        workAddress,
                        workSize,
                    };
                    output[0] = handle != 0u ? handle : 1u;
                    break;
                }
                case 35u: // SJRMT_DESTROY
                {
                    uint32_t handle = 0u;
                    (void)readSendWord(0u, handle);
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_sjrmtByHandle.erase(handle);
                    output[0] = 1u;
                    break;
                }
                case 36u: // SJRMT_GET_UUID
                {
                    uint32_t handle = 0u;
                    (void)readSendWord(0u, handle);
                    std::lock_guard<std::mutex> lock(m_mutex);
                    const auto it = m_sjrmtByHandle.find(handle);
                    if (it != m_sjrmtByHandle.end())
                    {
                        output[0] = it->second.uuid0;
                        output[1] = it->second.uuid1;
                        output[2] = it->second.uuid2;
                        output[3] = it->second.uuid3;
                    }
                    else
                    {
                        output = {0u, 0u, 0u, 0u};
                    }
                    outputWordCount = 4u;
                    break;
                }
                case 37u: // SJRMT_RESET
                {
                    uint32_t handle = 0u;
                    (void)readSendWord(0u, handle);
                    std::lock_guard<std::mutex> lock(m_mutex);
                    const auto it = m_sjrmtByHandle.find(handle);
                    if (it != m_sjrmtByHandle.end())
                    {
                        const uint32_t capacity = it->second.workSize == 0u
                                                      ? kDefaultSjrmtCapacity
                                                      : it->second.workSize;
                        it->second.readPosition = 0u;
                        it->second.writePosition = 0u;
                        it->second.roomBytes = capacity;
                        it->second.dataBytes = 0u;
                    }
                    output[0] = 1u;
                    break;
                }
                case 38u: // SJRMT_GET_CHUNK
                {
                    uint32_t handle = 0u;
                    uint32_t streamId = 0u;
                    uint32_t requestedBytes = 0u;
                    (void)readSendWord(0u, handle);
                    (void)readSendWord(1u, streamId);
                    (void)readSendWord(2u, requestedBytes);

                    uint32_t pointer = 0u;
                    uint32_t length = 0u;
                    std::lock_guard<std::mutex> lock(m_mutex);
                    const auto it = m_sjrmtByHandle.find(handle);
                    if (it != m_sjrmtByHandle.end())
                    {
                        SjrmtState &state = it->second;
                        const uint32_t capacity = state.workSize == 0u
                                                      ? kDefaultSjrmtCapacity
                                                      : state.workSize;
                        if (streamId == 0u)
                        {
                            length = std::min(requestedBytes, state.roomBytes);
                            pointer = state.workAddress +
                                      (capacity != 0u ? state.writePosition % capacity : 0u);
                            if (capacity != 0u)
                            {
                                state.writePosition = (state.writePosition + length) % capacity;
                            }
                            state.roomBytes -= length;
                        }
                        else if (streamId == 1u)
                        {
                            length = std::min(requestedBytes, state.dataBytes);
                            pointer = state.workAddress +
                                      (capacity != 0u ? state.readPosition % capacity : 0u);
                            if (capacity != 0u)
                            {
                                state.readPosition = (state.readPosition + length) % capacity;
                            }
                            state.dataBytes -= length;
                        }
                    }
                    output[0] = pointer;
                    output[1] = length;
                    outputWordCount = 2u;
                    break;
                }
                case 39u: // SJRMT_UNGET_CHUNK
                {
                    uint32_t handle = 0u;
                    uint32_t streamId = 0u;
                    uint32_t length = 0u;
                    (void)readSendWord(0u, handle);
                    (void)readSendWord(1u, streamId);
                    (void)readSendWord(3u, length);

                    std::lock_guard<std::mutex> lock(m_mutex);
                    const auto it = m_sjrmtByHandle.find(handle);
                    if (it != m_sjrmtByHandle.end())
                    {
                        SjrmtState &state = it->second;
                        const uint32_t capacity = state.workSize == 0u
                                                      ? kDefaultSjrmtCapacity
                                                      : state.workSize;
                        const uint32_t delta = capacity == 0u ? 0u : length % capacity;
                        if (streamId == 0u)
                        {
                            if (capacity != 0u)
                            {
                                state.writePosition =
                                    (state.writePosition + capacity - delta) % capacity;
                            }
                            state.roomBytes = std::min(capacity, state.roomBytes + length);
                        }
                        else if (streamId == 1u)
                        {
                            if (capacity != 0u)
                            {
                                state.readPosition =
                                    (state.readPosition + capacity - delta) % capacity;
                            }
                            state.dataBytes = std::min(capacity, state.dataBytes + length);
                        }
                    }
                    output[0] = 1u;
                    break;
                }
                case 40u: // SJRMT_PUT_CHUNK
                {
                    uint32_t handle = 0u;
                    uint32_t streamId = 0u;
                    uint32_t length = 0u;
                    (void)readSendWord(0u, handle);
                    (void)readSendWord(1u, streamId);
                    (void)readSendWord(3u, length);

                    std::lock_guard<std::mutex> lock(m_mutex);
                    const auto it = m_sjrmtByHandle.find(handle);
                    if (it != m_sjrmtByHandle.end())
                    {
                        SjrmtState &state = it->second;
                        const uint32_t capacity = state.workSize == 0u
                                                      ? kDefaultSjrmtCapacity
                                                      : state.workSize;
                        if (streamId == 0u)
                        {
                            state.roomBytes = std::min(capacity, state.roomBytes + length);
                        }
                        else if (streamId == 1u)
                        {
                            state.dataBytes = std::min(capacity, state.dataBytes + length);
                        }
                    }
                    output[0] = 1u;
                    break;
                }
                case 41u: // SJRMT_GET_NUM_DATA
                {
                    uint32_t handle = 0u;
                    uint32_t streamId = 0u;
                    (void)readSendWord(0u, handle);
                    (void)readSendWord(1u, streamId);

                    std::lock_guard<std::mutex> lock(m_mutex);
                    const auto it = m_sjrmtByHandle.find(handle);
                    output[0] = it != m_sjrmtByHandle.end()
                                    ? (streamId == 0u ? it->second.roomBytes
                                                      : it->second.dataBytes)
                                    : 0u;
                    break;
                }
                case 42u: // SJRMT_IS_GET_CHUNK
                {
                    uint32_t handle = 0u;
                    uint32_t streamId = 0u;
                    uint32_t requestedBytes = 0u;
                    (void)readSendWord(0u, handle);
                    (void)readSendWord(1u, streamId);
                    (void)readSendWord(2u, requestedBytes);

                    uint32_t available = 0u;
                    std::lock_guard<std::mutex> lock(m_mutex);
                    const auto it = m_sjrmtByHandle.find(handle);
                    if (it != m_sjrmtByHandle.end())
                    {
                        available = streamId == 0u ? it->second.roomBytes
                                                   : it->second.dataBytes;
                    }
                    output[0] = available >= requestedBytes ? 1u : 0u;
                    output[1] = available;
                    outputWordCount = 2u;
                    break;
                }
                case 43u: // SJRMT_INIT
                case 44u: // SJRMT_FINISH
                    output[0] = 1u;
                    break;
                default:
                {
                    uint32_t value = 1u;
                    if (request.send.address != 0u &&
                        request.send.size >= sizeof(uint32_t))
                    {
                        (void)readGuestPod(m_host, request.send.address, value);
                    }
                    if (command == 0u)
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        value = allocateUrpcHandleLocked();
                    }
                    output[0] = value != 0u ? value : 1u;
                    break;
                }
                }

                if (request.receive.address != 0u && request.receive.size > 0u)
                {
                    const uint32_t receiveWordCapacity =
                        request.receive.size / sizeof(uint32_t);
                    const uint32_t wordsToWrite =
                        std::min(outputWordCount, receiveWordCapacity);
                    for (uint32_t index = 0u; index < wordsToWrite; ++index)
                    {
                        (void)writeGuestPod(m_host,
                                            request.receive.address +
                                                (index * sizeof(uint32_t)),
                                            output[index]);
                    }

                    // The original HLE deliberately makes rbuf[1] available to command
                    // 42 callers even when their declared output word count is one.
                    if (command == 42u && outputWordCount > 1u)
                    {
                        (void)writeGuestPod(m_host,
                                            request.receive.address + sizeof(uint32_t),
                                            output[1]);
                    }

                    const uint32_t writtenBytes = wordsToWrite * sizeof(uint32_t);
                    if (request.receive.size > writtenBytes)
                    {
                        (void)m_host.zeroGuest(request.receive.address + writtenBytes,
                                               request.receive.size - writtenBytes);
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    ++m_urpcCalls;
                }
                result.handled = true;
                result.resultAddress = request.receive.address;
                return result;
            }

            bool hasReadableRange(uint32_t address, uint32_t length) const
            {
                if (length == 0u)
                {
                    return true;
                }
                const uint32_t lastAddress = address + length - 1u;
                if (address > lastAddress)
                {
                    return false;
                }

                uint8_t byte = 0u;
                return m_host.readGuest(address, &byte, sizeof(byte)) &&
                       m_host.readGuest(lastAddress, &byte, sizeof(byte));
            }

            bool readValidCommandCount(uint32_t workAddress,
                                       uint32_t workSize,
                                       uint32_t &commandCount) const
            {
                commandCount = 0u;
                if (workSize < kMinimumDtxWorkSize ||
                    !readGuestPod(m_host, workAddress, commandCount) ||
                    commandCount == 0u ||
                    commandCount > kMaxDtxCommands)
                {
                    return false;
                }

                const uint64_t commandBytes =
                    static_cast<uint64_t>(commandCount) * kDtxCommandSize;
                const uint64_t requiredBytes =
                    kDtxHeaderSize + commandBytes + sizeof(uint32_t);
                return requiredBytes <= workSize;
            }

            bool looksLikeSjxPayloadLocked(uint32_t workAddress, uint32_t workSize) const
            {
                uint32_t commandCount = 0u;
                if (!readValidCommandCount(workAddress, workSize, commandCount))
                {
                    return false;
                }

                for (uint32_t index = 0u; index < commandCount; ++index)
                {
                    const uint32_t commandAddress =
                        workAddress + kDtxHeaderSize + (index * kDtxCommandSize);
                    std::array<uint8_t, kDtxCommandSize> command{};
                    if (!m_host.readGuest(commandAddress, command.data(), command.size()))
                    {
                        break;
                    }

                    const uint8_t commandNumber = command[0];
                    const uint8_t line = command[1];
                    uint16_t xid = 0u;
                    uint32_t sjxHandle = 0u;
                    uint32_t chunkDataAddress = 0u;
                    uint32_t chunkLength = 0u;
                    std::memcpy(&xid, command.data() + 2u, sizeof(xid));
                    std::memcpy(&sjxHandle, command.data() + 4u, sizeof(sjxHandle));
                    std::memcpy(&chunkDataAddress, command.data() + 8u,
                                sizeof(chunkDataAddress));
                    std::memcpy(&chunkLength, command.data() + 12u, sizeof(chunkLength));

                    if (commandNumber != 0u || chunkLength == 0u ||
                        !hasReadableRange(chunkDataAddress, chunkLength))
                    {
                        continue;
                    }

                    const auto sjx = m_sjxByHandle.find(sjxHandle);
                    if (sjx == m_sjxByHandle.end() ||
                        (sjx->second.xid != 0u && sjx->second.xid != xid) ||
                        line != sjx->second.line ||
                        m_sjrmtByHandle.find(sjx->second.destinationSjHandle) ==
                            m_sjrmtByHandle.end())
                    {
                        continue;
                    }
                    return true;
                }
                return false;
            }

            bool looksLikePs2RnaPayloadLocked(uint32_t workAddress,
                                              uint32_t workSize) const
            {
                uint32_t commandCount = 0u;
                if (!readValidCommandCount(workAddress, workSize, commandCount))
                {
                    return false;
                }

                for (uint32_t index = 0u; index < commandCount; ++index)
                {
                    const uint32_t commandAddress =
                        workAddress + kDtxHeaderSize + (index * kDtxCommandSize);
                    std::array<uint8_t, kDtxCommandSize> command{};
                    if (!m_host.readGuest(commandAddress, command.data(), command.size()))
                    {
                        break;
                    }

                    uint16_t commandNumber = 0u;
                    uint32_t rnaHandle = 0u;
                    std::memcpy(&commandNumber, command.data(), sizeof(commandNumber));
                    std::memcpy(&rnaHandle, command.data() + 4u, sizeof(rnaHandle));
                    if (commandNumber <= 5u &&
                        m_ps2RnaByHandle.find(rnaHandle) != m_ps2RnaByHandle.end())
                    {
                        return true;
                    }
                }
                return false;
            }

            bool inferTransferFromPayloadLocked(uint32_t sourceAddress,
                                                uint32_t destinationAddress,
                                                uint32_t size,
                                                TransferState &result) const
            {
                if (m_transferById.empty())
                {
                    return false;
                }

                if (looksLikeSjxPayloadLocked(sourceAddress, size))
                {
                    result = TransferState{0u, 0u, sourceAddress, destinationAddress, size};
                    return true;
                }
                if (looksLikePs2RnaPayloadLocked(sourceAddress, size))
                {
                    result = TransferState{1u, 0u, sourceAddress, destinationAddress, size};
                    return true;
                }
                return false;
            }

            bool copyBytes(uint32_t destinationAddress,
                           uint32_t sourceAddress,
                           uint32_t length)
            {
                for (uint32_t index = 0u; index < length; ++index)
                {
                    uint8_t value = 0u;
                    if (!m_host.readGuest(sourceAddress + index, &value, sizeof(value)) ||
                        !m_host.writeGuest(destinationAddress + index, &value, sizeof(value)))
                    {
                        return false;
                    }
                }
                return true;
            }

            void appendToSjrmtData(SjrmtState &state,
                                   uint32_t sourceDataAddress,
                                   uint32_t requestedLength)
            {
                const uint32_t capacity = state.workSize == 0u
                                              ? kDefaultSjrmtCapacity
                                              : state.workSize;
                if (capacity == 0u || state.workAddress == 0u ||
                    state.roomBytes == 0u || requestedLength == 0u)
                {
                    return;
                }

                const uint32_t requestedCopyLength =
                    std::min(requestedLength, state.roomBytes);
                uint32_t copiedLength = 0u;
                for (uint32_t index = 0u; index < requestedCopyLength; ++index)
                {
                    const uint32_t writeOffset = (state.writePosition + index) % capacity;
                    if (!copyBytes(state.workAddress + writeOffset,
                                   sourceDataAddress + index,
                                   1u))
                    {
                        break;
                    }
                    ++copiedLength;
                }

                state.writePosition = (state.writePosition + copiedLength) % capacity;
                state.roomBytes -= copiedLength;
                state.dataBytes = std::min(capacity, state.dataBytes + copiedLength);
            }

            static void consumeSjrmtData(SjrmtState &state)
            {
                const uint32_t capacity = state.workSize == 0u
                                              ? kDefaultSjrmtCapacity
                                              : state.workSize;
                if (capacity == 0u || state.dataBytes == 0u)
                {
                    return;
                }

                const uint32_t consumed = std::min(capacity, state.dataBytes);
                state.readPosition = (state.readPosition + consumed) % capacity;
                state.dataBytes -= consumed;
                state.roomBytes = std::min(capacity, state.roomBytes + consumed);
            }

            void consumeActivePs2RnaStreamsLocked()
            {
                for (const auto &[handle, rna] : m_ps2RnaByHandle)
                {
                    (void)handle;
                    if (!rna.playEnabled)
                    {
                        continue;
                    }

                    auto consumeHandle = [&](uint32_t sjHandle)
                    {
                        if (sjHandle == 0u)
                        {
                            return;
                        }
                        const auto it = m_sjrmtByHandle.find(sjHandle);
                        if (it != m_sjrmtByHandle.end())
                        {
                            consumeSjrmtData(it->second);
                        }
                    };
                    consumeHandle(rna.sjHandle0);
                    consumeHandle(rna.sjHandle1);
                }
            }

            void applySjxPayload(const TransferState &transfer)
            {
                if (transfer.eeWorkAddress == 0u ||
                    transfer.workSize < kMinimumDtxWorkSize)
                {
                    return;
                }

                uint32_t commandCount = 0u;
                if (!readGuestPod(m_host, transfer.eeWorkAddress, commandCount))
                {
                    return;
                }
                commandCount = std::min(commandCount, kMaxDtxCommands);
                if (commandCount == 0u)
                {
                    return;
                }

                std::lock_guard<std::mutex> lock(m_mutex);
                for (uint32_t index = 0u; index < commandCount; ++index)
                {
                    const uint32_t commandAddress =
                        transfer.eeWorkAddress + kDtxHeaderSize +
                        (index * kDtxCommandSize);
                    std::array<uint8_t, kDtxCommandSize> command{};
                    if (!m_host.readGuest(commandAddress, command.data(), command.size()))
                    {
                        break;
                    }

                    const uint8_t commandNumber = command[0];
                    const uint8_t line = command[1];
                    uint16_t xid = 0u;
                    uint32_t sjxHandle = 0u;
                    uint32_t chunkDataAddress = 0u;
                    uint32_t chunkLength = 0u;
                    std::memcpy(&xid, command.data() + 2u, sizeof(xid));
                    std::memcpy(&sjxHandle, command.data() + 4u, sizeof(sjxHandle));
                    std::memcpy(&chunkDataAddress, command.data() + 8u,
                                sizeof(chunkDataAddress));
                    std::memcpy(&chunkLength, command.data() + 12u, sizeof(chunkLength));

                    if (commandNumber != 0u || chunkLength == 0u)
                    {
                        continue;
                    }

                    const auto sjx = m_sjxByHandle.find(sjxHandle);
                    if (sjx == m_sjxByHandle.end() ||
                        (sjx->second.xid != 0u && sjx->second.xid != xid))
                    {
                        continue;
                    }
                    const auto sjrmt =
                        m_sjrmtByHandle.find(sjx->second.destinationSjHandle);
                    if (sjrmt == m_sjrmtByHandle.end() || line != sjx->second.line)
                    {
                        continue;
                    }

                    appendToSjrmtData(sjrmt->second,
                                      chunkDataAddress,
                                      chunkLength);
                    constexpr uint8_t roomLine = 0u;
                    (void)m_host.writeGuest(commandAddress + 1u,
                                            &roomLine,
                                            sizeof(roomLine));
                }
                consumeActivePs2RnaStreamsLocked();
            }

            void applyPs2RnaPayload(const TransferState &transfer)
            {
                if (transfer.eeWorkAddress == 0u ||
                    transfer.workSize < kMinimumDtxWorkSize)
                {
                    return;
                }

                uint32_t commandCount = 0u;
                if (!readGuestPod(m_host, transfer.eeWorkAddress, commandCount))
                {
                    return;
                }
                commandCount = std::min(commandCount, kMaxDtxCommands);
                if (commandCount == 0u)
                {
                    return;
                }

                std::lock_guard<std::mutex> lock(m_mutex);
                for (uint32_t index = 0u; index < commandCount; ++index)
                {
                    const uint32_t commandAddress =
                        transfer.eeWorkAddress + kDtxHeaderSize +
                        (index * kDtxCommandSize);
                    std::array<uint8_t, kDtxCommandSize> command{};
                    if (!m_host.readGuest(commandAddress, command.data(), command.size()))
                    {
                        break;
                    }

                    uint16_t commandNumber = 0u;
                    uint32_t rnaHandle = 0u;
                    uint32_t argument1 = 0u;
                    uint32_t argument2 = 0u;
                    std::memcpy(&commandNumber, command.data(), sizeof(commandNumber));
                    std::memcpy(&rnaHandle, command.data() + 4u, sizeof(rnaHandle));
                    std::memcpy(&argument1, command.data() + 8u, sizeof(argument1));
                    std::memcpy(&argument2, command.data() + 12u, sizeof(argument2));

                    const auto it = m_ps2RnaByHandle.find(rnaHandle);
                    if (it == m_ps2RnaByHandle.end())
                    {
                        continue;
                    }

                    Ps2RnaState &state = it->second;
                    switch (commandNumber)
                    {
                    case 0u:
                        state.playEnabled = true;
                        break;
                    case 1u:
                        state.playEnabled = false;
                        break;
                    case 2u:
                        state.playEnabled = argument1 != 0u;
                        break;
                    case 3u:
                        state.channelCount = argument1;
                        break;
                    case 4u:
                        state.sampleFrequency = argument1;
                        break;
                    case 5u:
                        state.volume = argument2;
                        break;
                    default:
                        break;
                    }
                }
                consumeActivePs2RnaStreamsLocked();
            }

            IopHost &m_host;
            CriDtxBindings m_bindings;
            mutable std::mutex m_mutex;
            std::unordered_map<uint32_t, uint32_t> m_remoteById;
            std::unordered_map<uint32_t, TransferState> m_transferById;
            std::unordered_map<uint32_t, SjxState> m_sjxByHandle;
            std::unordered_map<uint32_t, Ps2RnaState> m_ps2RnaByHandle;
            std::unordered_map<uint32_t, SjrmtState> m_sjrmtByHandle;
            uint32_t m_nextUrpcObject = 0u;
            uint64_t m_dmaAcks = 0u;
            uint64_t m_dmaMisses = 0u;
            uint64_t m_urpcCalls = 0u;
            const std::array<uint32_t, 1> m_sids;
        };
    }

    std::unique_ptr<IopService> createCriDtxService(IopHost &host,
                                                    CriDtxBindings bindings)
    {
        if (bindings.serviceName.empty() || bindings.sid == 0u ||
            bindings.urpcObjectBase == 0u ||
            bindings.urpcObjectLimit <= bindings.urpcObjectBase ||
            bindings.urpcObjectStride == 0u ||
            bindings.urpcFunctionTableBase == 0u ||
            bindings.urpcObjectTableBase == 0u ||
            bindings.dispatcherFunctionAddress == 0u ||
            bindings.rpcServerPoolBase == 0u ||
            bindings.rpcServerStride == 0u)
        {
            throw std::invalid_argument("invalid CRI DTX bindings");
        }
        return std::make_unique<CriDtxService>(host, std::move(bindings));
    }
}
