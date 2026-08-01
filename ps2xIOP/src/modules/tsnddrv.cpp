#include "../module_factories.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kStatusSize = 0x42u;
        constexpr uint32_t kSeInfoOffset = 0x00u;
        constexpr uint32_t kMidiInfoOffset = 0x0Cu;
        constexpr uint32_t kMidiSumOffset = 0x1Eu;
        constexpr uint32_t kSeSumOffset = 0x26u;
        constexpr uint32_t kAddressTableEntries = 16u;
        constexpr uint32_t alignUp(uint32_t value, uint32_t alignment)
        {
            if (alignment == 0u)
            {
                return value;
            }
            return (value + (alignment - 1u)) & ~(alignment - 1u);
        }

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

        template <typename T, size_t Size>
        bool hasAnyNonZero(const std::array<T, Size> &values)
        {
            return std::any_of(values.begin(), values.end(), [](const T value)
                               { return value != static_cast<T>(0); });
        }

        size_t commandLength(uint8_t command)
        {
            const uint8_t hi = static_cast<uint8_t>(command & 0xF0u);
            switch (hi)
            {
            case 0x00u:
            {
                size_t length = 4u;
                if ((command & 0x01u) != 0u)
                {
                    ++length;
                }
                if ((command & 0x02u) != 0u)
                {
                    ++length;
                }
                if ((command & 0x04u) != 0u)
                {
                    length += 2u;
                }
                return length;
            }
            case 0x10u:
                return command == 0x11u ? 3u : 1u;
            case 0x20u:
                if (command == 0x22u || command == 0x23u || command == 0x24u || command == 0x25u)
                {
                    return 3u;
                }
                if (command == 0x26u)
                {
                    return 4u;
                }
                if (command == 0x20u)
                {
                    return 5u;
                }
                if (command == 0x27u || command == 0x28u || command == 0x29u ||
                    command == 0x2Cu || command == 0x2Du)
                {
                    return 8u;
                }
                return 2u;
            case 0x40u:
                if (command == 0x47u || command == 0x48u || command == 0x49u || command == 0x4Au ||
                    command == 0x41u || command == 0x42u)
                {
                    return 2u;
                }
                if (command == 0x4Bu)
                {
                    return 3u;
                }
                if (command == 0x45u || command == 0x4Cu)
                {
                    return 4u;
                }
                if (command == 0x44u)
                {
                    return 6u;
                }
                if (command == 0x4Du || command == 0x4Eu)
                {
                    return 3u;
                }
                if (command == 0x4Fu)
                {
                    return 6u;
                }
                return 1u;
            case 0x50u:
            case 0x60u:
                if (command == 0x51u || command == 0x52u || command == 0x53u || command == 0x54u)
                {
                    return 8u;
                }
                return 2u;
            default:
                return 0u;
            }
        }

        class TsnddrvService final : public IopService
        {
        public:
            TsnddrvService(IopHost &host, TsnddrvBindings bindings)
                : m_host(host), m_bindings(std::move(bindings))
            {
                m_sids.push_back(m_bindings.commandSid);
                if (m_bindings.stateSid != m_bindings.commandSid)
                {
                    m_sids.push_back(m_bindings.stateSid);
                }
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
                m_state = {};
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                RpcResult result{};

                if (request.sid != m_bindings.commandSid && request.sid != m_bindings.stateSid)
                {
                    return result;
                }

                if (request.sid == m_bindings.commandSid && request.function == m_bindings.submitFunction)
                {
                    handleCommandBuffer(request.send);
                    result.handled = true;
                }
                else if (request.sid == m_bindings.stateSid &&
                         (request.function == m_bindings.getStatusFunction ||
                          request.function == m_bindings.getAddrTableFunction))
                {
                    uint32_t responseAddress = 0u;
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        if (!ensureMemoryLocked())
                        {
                            return result;
                        }
                        responseAddress = request.function == m_bindings.getStatusFunction
                                              ? m_state.statusAddress
                                              : m_state.addressTableAddress;
                    }

                    if (request.receive.address != 0u && request.receive.size >= sizeof(uint32_t))
                    {
                        (void)writeGuestPod(m_host, request.receive.address, responseAddress);
                        if (request.receive.size > sizeof(uint32_t))
                        {
                            (void)m_host.zeroGuest(request.receive.address + sizeof(uint32_t),
                                                   request.receive.size - sizeof(uint32_t));
                        }
                        result.resultAddress = request.receive.address;
                    }

                    result.handled = true;
                    result.signalNowaitCompletion = true;
                }
                else if (m_bindings.streamOpenFunction != 0u &&
                         request.function == m_bindings.streamOpenFunction)
                {
                    if (m_bindings.streamStateAddress != 0u)
                    {
                        (void)writeGuestPod(m_host, m_bindings.streamStateAddress, m_bindings.streamReadyValue);
                    }
                    if (request.receive.address != 0u && request.receive.size >= sizeof(uint32_t))
                    {
                        constexpr uint32_t zero = 0u;
                        (void)writeGuestPod(m_host, request.receive.address, zero);
                        result.resultAddress = request.receive.address;
                    }
                    result.handled = true;
                    result.signalNowaitCompletion = true;
                }
                else if (m_bindings.channelConfigFunction != 0u &&
                         request.function == m_bindings.channelConfigFunction)
                {
                    uint32_t channel = 0u;
                    if (request.send.address != 0u)
                    {
                        (void)readGuestPod(m_host, request.send.address, channel);
                    }
                    if (m_bindings.channelAllocFlagTableAddress != 0u && channel < 16u)
                    {
                        constexpr uint32_t allocated = 1u;
                        (void)writeGuestPod(m_host,
                                            m_bindings.channelAllocFlagTableAddress + channel * sizeof(uint32_t),
                                            allocated);
                    }
                    if (request.receive.address != 0u && request.receive.size >= sizeof(uint32_t))
                    {
                        constexpr uint32_t zero = 0u;
                        (void)writeGuestPod(m_host, request.receive.address, zero);
                        result.resultAddress = request.receive.address;
                    }
                    result.handled = true;
                    result.signalNowaitCompletion = true;
                }
                else if (m_bindings.stopFunction != 0u && request.function == m_bindings.stopFunction)
                {
                    if (m_bindings.stopCompletionFlagAddress != 0u)
                    {
                        constexpr uint32_t stopped = 1u;
                        (void)writeGuestPod(m_host, m_bindings.stopCompletionFlagAddress, stopped);
                    }
                    if (request.receive.address != 0u && request.receive.size >= sizeof(uint32_t))
                    {
                        constexpr uint32_t zero = 0u;
                        (void)writeGuestPod(m_host, request.receive.address, zero);
                        result.resultAddress = request.receive.address;
                    }
                    result.handled = true;
                    result.signalNowaitCompletion = true;
                }
                else if (request.receive.address != 0u && request.receive.size >= sizeof(uint32_t))
                {
                    // Unrecognized subcommand on a service SID we own: leave the request
                    // unhandled (so a guest-registered server function at this SID, if any,
                    // still runs) but hand back a benign status word rather than stale/zeroed
                    // memory, matching real hardware's tolerance of unknown subcommands.
                    (void)writeGuestPod(m_host, request.receive.address, m_bindings.benignStatusValue);
                }

                if (result.handled)
                {
                    const auto rule = std::find_if(
                        m_bindings.completionRules.begin(),
                        m_bindings.completionRules.end(),
                        [&](const TsnddrvCompletionRule &candidate) {
                            return candidate.eeFunction == request.endFunction;
                        });
                    if (rule != m_bindings.completionRules.end())
                    {
                        if (rule->suppressGuestCallback)
                        {
                            result.callbackPolicy = CallbackPolicy::Suppress;
                        }
                        result.signalCompletion = rule->signalCompletion;
                        if (rule->clearBusy)
                        {
                            constexpr uint32_t idle = 0u;
                            (void)writeGuestPod(m_host,
                                                m_bindings.busyFlagAddress,
                                                idle);
                        }
                    }
                }

                return result;
            }

            void onSifTransfer(const SifTransfer &transfer) override
            {
                if (transfer.kind != SifTransferKind::GetOtherData ||
                    transfer.phase != SifTransferPhase::BeforeCopy ||
                    transfer.size != kStatusSize)
                {
                    return;
                }

                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_state.initialized || transfer.sourceAddress != m_state.statusAddress)
                {
                    return;
                }
                backfillStatusLocked();
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                metrics.push_back({"initialized", m_state.initialized ? 1u : 0u, false});
                metrics.push_back({"status_address", m_state.statusAddress, true});
                metrics.push_back({"address_table", m_state.addressTableAddress, true});
                metrics.push_back({"hd_base", m_state.hdBaseAddress, true});
                metrics.push_back({"sq_base", m_state.sqBaseAddress, true});
                metrics.push_back({"data_base", m_state.dataBaseAddress, true});
            }

        private:
            struct State
            {
                bool initialized = false;
                uint32_t storageBaseAddress = 0u;
                uint32_t storageSize = 0u;
                uint32_t statusAddress = 0u;
                uint32_t addressTableAddress = 0u;
                uint32_t hdBaseAddress = 0u;
                uint32_t sqBaseAddress = 0u;
                uint32_t dataBaseAddress = 0u;
            };

            bool ensureMemoryLocked()
            {
                if (m_state.statusAddress == 0u)
                {
                    const TsnddrvGuestArena &arena = m_bindings.arena;
                    const uint32_t statusAddress = alignUp(arena.base, arena.statusAlignment);
                    const uint32_t addressTableAddress =
                        alignUp(statusAddress + kStatusSize, arena.tableAlignment);
                    const uint32_t hdBaseAddress =
                        alignUp(addressTableAddress + (kAddressTableEntries * sizeof(uint32_t)),
                                arena.storageAlignment);
                    const uint32_t sqBaseAddress =
                        alignUp(hdBaseAddress + arena.hdBytes, arena.storageAlignment);
                    const uint32_t dataBaseAddress =
                        alignUp(sqBaseAddress + arena.sqBytes, arena.storageAlignment);
                    const uint32_t storageEnd = dataBaseAddress + arena.dataBytes;
                    if (storageEnd > arena.limit)
                    {
                        return false;
                    }

                    m_state.statusAddress = statusAddress;
                    m_state.addressTableAddress = addressTableAddress;
                    m_state.hdBaseAddress = hdBaseAddress;
                    m_state.sqBaseAddress = sqBaseAddress;
                    m_state.dataBaseAddress = dataBaseAddress;
                    m_state.storageBaseAddress = hdBaseAddress;
                    m_state.storageSize = storageEnd - hdBaseAddress;
                }

                if (m_state.statusAddress == 0u ||
                    m_state.addressTableAddress == 0u ||
                    m_state.storageBaseAddress == 0u)
                {
                    return false;
                }

                if (!m_state.initialized)
                {
                    if (!m_host.zeroGuest(m_state.statusAddress, kStatusSize) ||
                        !m_host.zeroGuest(m_state.addressTableAddress,
                                          kAddressTableEntries * sizeof(uint32_t)) ||
                        !m_host.zeroGuest(m_state.storageBaseAddress, m_state.storageSize))
                    {
                        return false;
                    }

                    if (!writeGuestPod(m_host,
                                       m_state.addressTableAddress + (0u * sizeof(uint32_t)),
                                       m_state.hdBaseAddress) ||
                        !writeGuestPod(m_host,
                                       m_state.addressTableAddress + (1u * sizeof(uint32_t)),
                                       m_state.sqBaseAddress) ||
                        !writeGuestPod(m_host,
                                       m_state.addressTableAddress + (2u * sizeof(uint32_t)),
                                       m_state.dataBaseAddress))
                    {
                        return false;
                    }
                    m_state.initialized = true;
                }

                return true;
            }

            int16_t checkValue(bool seTable,
                               uint32_t index,
                               uint32_t count) const
            {
                if (index >= count)
                {
                    return 0;
                }

                for (const TsnddrvChecksumTables &candidate : m_bindings.checksumCandidates)
                {
                    const uint32_t base = seTable ? candidate.seAddress : candidate.midiAddress;
                    int16_t value = 0;
                    if (readGuestPod(m_host,
                                     base + (index * sizeof(int16_t)),
                                     value) &&
                        value != 0)
                    {
                        return value;
                    }
                }
                return 0;
            }

            bool selectCompatChecks(uint32_t &seBase, uint32_t &midiBase) const
            {
                const TsnddrvChecksumTables *firstReadable = nullptr;
                for (const TsnddrvChecksumTables &candidate : m_bindings.checksumCandidates)
                {
                    std::array<int16_t, 5> seValues{};
                    std::array<int16_t, 4> midiValues{};
                    const bool seReadable = m_host.readGuest(candidate.seAddress,
                                                             seValues.data(),
                                                             sizeof(seValues));
                    const bool midiReadable = m_host.readGuest(candidate.midiAddress,
                                                               midiValues.data(),
                                                               sizeof(midiValues));
                    if (seReadable && midiReadable && !firstReadable)
                    {
                        firstReadable = &candidate;
                    }
                    const bool looksLive =
                        (seReadable && hasAnyNonZero(seValues)) ||
                        (midiReadable && hasAnyNonZero(midiValues));
                    if (seReadable && midiReadable && looksLive)
                    {
                        seBase = candidate.seAddress;
                        midiBase = candidate.midiAddress;
                        return true;
                    }
                }

                if (firstReadable)
                {
                    seBase = firstReadable->seAddress;
                    midiBase = firstReadable->midiAddress;
                    return true;
                }
                return false;
            }

            void backfillStatusLocked()
            {
                uint32_t seBase = 0u;
                uint32_t midiBase = 0u;
                if (!selectCompatChecks(seBase, midiBase))
                {
                    return;
                }

                auto backfillSlots = [&](uint32_t statusOffset,
                                         uint32_t compatBase,
                                         uint32_t slotCount)
                {
                    for (uint32_t slot = 0u; slot < slotCount; ++slot)
                    {
                        int16_t liveValue = 0;
                        if (!readGuestPod(m_host,
                                          m_state.statusAddress + statusOffset +
                                              (slot * sizeof(int16_t)),
                                          liveValue) ||
                            liveValue != 0)
                        {
                            continue;
                        }

                        int16_t compatValue = 0;
                        if (!readGuestPod(m_host,
                                          compatBase + (slot * sizeof(int16_t)),
                                          compatValue) ||
                            compatValue == 0)
                        {
                            continue;
                        }

                        (void)writeGuestPod(m_host,
                                            m_state.statusAddress + statusOffset +
                                                (slot * sizeof(int16_t)),
                                            compatValue);
                    }
                };

                backfillSlots(kSeSumOffset, seBase, 5u);
                backfillSlots(kMidiSumOffset, midiBase, 4u);
            }

            void applyCommandLocked(const std::array<uint8_t, 8> &command)
            {
                if (m_state.statusAddress == 0u)
                {
                    return;
                }

                switch (command[0])
                {
                case 0x20u: // SdrBgmReq
                {
                    const uint32_t port = command[1] & 0x0Fu;
                    uint16_t midiInfo = 0u;
                    (void)readGuestPod(m_host,
                                       m_state.statusAddress + kMidiInfoOffset,
                                       midiInfo);
                    midiInfo = static_cast<uint16_t>(midiInfo |
                                                     static_cast<uint16_t>(1u << port));
                    (void)writeGuestPod(m_host,
                                        m_state.statusAddress + kMidiInfoOffset,
                                        midiInfo);
                    break;
                }
                case 0x21u: // SdrBgmStop
                {
                    const uint32_t port = command[1] & 0x0Fu;
                    uint16_t midiInfo = 0u;
                    (void)readGuestPod(m_host,
                                       m_state.statusAddress + kMidiInfoOffset,
                                       midiInfo);
                    midiInfo = static_cast<uint16_t>(midiInfo &
                                                     ~static_cast<uint16_t>(1u << port));
                    (void)writeGuestPod(m_host,
                                        m_state.statusAddress + kMidiInfoOffset,
                                        midiInfo);
                    break;
                }
                case 0x28u: // SdrHDDataSet
                {
                    const uint32_t port = command[1] & 0x0Fu;
                    if (port >= 4u)
                    {
                        break;
                    }
                    const int16_t checksum = checkValue(false, port, 4u);
                    (void)writeGuestPod(m_host,
                                        m_state.statusAddress + kMidiSumOffset +
                                            (port * sizeof(int16_t)),
                                        checksum);
                    break;
                }
                case 0x29u: // SdrHDDataSet2
                {
                    const uint32_t port = command[1] & 0x0Fu;
                    if (port >= 5u)
                    {
                        break;
                    }
                    const int16_t checksum = checkValue(true, port, 5u);
                    (void)writeGuestPod(m_host,
                                        m_state.statusAddress + kSeSumOffset +
                                            (port * sizeof(int16_t)),
                                        checksum);
                    break;
                }
                case 0x10u: // SdrSeAllStop
                    (void)m_host.zeroGuest(m_state.statusAddress + kSeInfoOffset,
                                           6u * sizeof(uint16_t));
                    break;
                default:
                    break;
                }
            }

            void handleCommandBuffer(GuestBuffer send)
            {
                if (send.address == 0u || send.size == 0u)
                {
                    return;
                }

                std::lock_guard<std::mutex> lock(m_mutex);
                if (!ensureMemoryLocked())
                {
                    return;
                }

                for (uint32_t offset = 0u; offset < send.size;)
                {
                    uint8_t operation = 0u;
                    if (!m_host.readGuest(send.address + offset, &operation, sizeof(operation)) ||
                        operation == 0xFFu)
                    {
                        break;
                    }

                    const size_t length = commandLength(operation);
                    if (length == 0u ||
                        static_cast<uint64_t>(offset) + length > send.size)
                    {
                        break;
                    }

                    std::array<uint8_t, 8> command{};
                    if (!m_host.readGuest(send.address + offset, command.data(), length))
                    {
                        break;
                    }
                    applyCommandLocked(command);
                    offset += static_cast<uint32_t>(length);
                }
            }

            IopHost &m_host;
            TsnddrvBindings m_bindings;
            mutable std::mutex m_mutex;
            State m_state;
            std::vector<uint32_t> m_sids;
        };
    }

    std::unique_ptr<IopService> createTsnddrvService(IopHost &host,
                                                     TsnddrvBindings bindings)
    {
        const auto isPowerOfTwo = [](uint32_t value) {
            return value != 0u && (value & (value - 1u)) == 0u;
        };
        const auto alignUp64 = [](uint64_t value, uint32_t alignment) {
            return (value + (alignment - 1u)) &
                   ~static_cast<uint64_t>(alignment - 1u);
        };

        const TsnddrvGuestArena &arena = bindings.arena;
        if (bindings.serviceName.empty() ||
            arena.base >= arena.limit ||
            !isPowerOfTwo(arena.statusAlignment) ||
            !isPowerOfTwo(arena.tableAlignment) ||
            !isPowerOfTwo(arena.storageAlignment) ||
            arena.hdBytes == 0u || arena.sqBytes == 0u || arena.dataBytes == 0u ||
            bindings.checksumCandidates.empty() ||
            bindings.getStatusFunction == bindings.getAddrTableFunction)
        {
            throw std::invalid_argument("invalid TSNDDRV bindings");
        }

        {
            // The optional extra subcommands (0 = disabled) must not alias the
            // always-on submit/getStatus/getAddrTable functions or each other, or
            // dispatch would become ambiguous/unreachable.
            std::unordered_set<uint32_t> reserved{bindings.submitFunction,
                                                   bindings.getStatusFunction,
                                                   bindings.getAddrTableFunction};
            for (const uint32_t extra : {bindings.streamOpenFunction,
                                         bindings.channelConfigFunction,
                                         bindings.stopFunction})
            {
                if (extra != 0u && !reserved.insert(extra).second)
                {
                    throw std::invalid_argument("colliding TSNDDRV subcommand function numbers");
                }
            }
        }

        uint64_t end = alignUp64(arena.base, arena.statusAlignment) + kStatusSize;
        end = alignUp64(end, arena.tableAlignment) +
              (kAddressTableEntries * sizeof(uint32_t));
        end = alignUp64(end, arena.storageAlignment) + arena.hdBytes;
        end = alignUp64(end, arena.storageAlignment) + arena.sqBytes;
        end = alignUp64(end, arena.storageAlignment) + arena.dataBytes;
        if (end > arena.limit || end > std::numeric_limits<uint32_t>::max())
        {
            throw std::invalid_argument("TSNDDRV guest arena is too small");
        }

        for (const TsnddrvChecksumTables &candidate : bindings.checksumCandidates)
        {
            if (candidate.seAddress == 0u || candidate.midiAddress == 0u)
            {
                throw std::invalid_argument("incomplete TSNDDRV checksum binding");
            }
        }

        std::unordered_set<uint32_t> callbacks;
        for (const TsnddrvCompletionRule &rule : bindings.completionRules)
        {
            if (rule.eeFunction == 0u || !callbacks.emplace(rule.eeFunction).second ||
                (rule.clearBusy && bindings.busyFlagAddress == 0u))
            {
                throw std::invalid_argument("invalid TSNDDRV completion rule");
            }
        }

        switch (bindings.protocol)
        {
        case TsnddrvProtocolVariant::SndQueueV1:
            break;
        }
        return std::make_unique<TsnddrvService>(host, std::move(bindings));
    }
}
