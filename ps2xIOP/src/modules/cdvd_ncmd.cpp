#include "module_factories.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <mutex>
#include <vector>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kSid = 0x80000595u;
        constexpr uint32_t kStreamFunction = 9u;
        constexpr uint32_t kDiskReadyFunction = 14u;
        constexpr uint32_t kStreamPacketBytes = 20u;
        constexpr uint32_t kSectorBytes = 2048u;
        constexpr uint32_t kMaxReadSectors = 0xFFFFu;
        constexpr uint32_t kChunkSectors = 32u;

        enum class StreamCommand : uint32_t
        {
            Start = 1u,
            Read = 2u,
            Stop = 3u,
            Seek = 4u,
            Init = 5u,
            Stat = 6u,
            Pause = 7u,
            Resume = 8u,
            SeekForward = 9u,
        };

        struct StreamPacket
        {
            uint32_t lsn = 0u;
            uint32_t sectors = 0u;
            uint32_t destination = 0u;
            uint32_t command = 0u;
            uint32_t readMode = 0u;
        };
        static_assert(sizeof(StreamPacket) == kStreamPacketBytes);

        class CdvdNcmdService final : public IopService
        {
        public:
            explicit CdvdNcmdService(IopHost &host)
                : m_host(host)
            {
            }

            [[nodiscard]] std::string_view name() const override
            {
                return "CD/DVD non-blocking commands";
            }

            [[nodiscard]] std::span<const uint32_t> sids() const override
            {
                return kSids;
            }

            void reset() override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_initialized = false;
                m_streaming = false;
                m_paused = false;
                m_currentLsn = 0u;
                m_streamCalls = 0u;
                m_readCalls = 0u;
                m_sectorsRead = 0u;
                m_diskReadyCalls = 0u;
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                if (request.sid != kSid || request.mode != 0u)
                {
                    return {};
                }
                if (request.function == kDiskReadyFunction)
                {
                    return handleDiskReady(request);
                }
                if (request.function == kStreamFunction)
                {
                    return handleStream(request);
                }
                return {};
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                metrics.push_back({"stream_calls", m_streamCalls, false});
                metrics.push_back({"stream_read_calls", m_readCalls, false});
                metrics.push_back({"stream_sectors_read", m_sectorsRead, false});
                metrics.push_back({"disk_ready_calls", m_diskReadyCalls, false});
                metrics.push_back({"stream_lsn", m_currentLsn, true});
                metrics.push_back({"streaming", m_streaming ? 1u : 0u, false});
            }

        private:
            [[nodiscard]] RpcResult handledResult(const RpcRequest &request) const
            {
                RpcResult result;
                result.handled = true;
                result.resultAddress = request.receive.address;
                return result;
            }

            [[nodiscard]] RpcResult handleDiskReady(const RpcRequest &request)
            {
                if (request.send.size != 0u ||
                    request.receive.address == 0u ||
                    request.receive.size < sizeof(uint32_t))
                {
                    return {};
                }

                constexpr uint32_t kReady = 2u;
                if (!m_host.writeGuest(request.receive.address,
                                       &kReady,
                                       sizeof(kReady)))
                {
                    return {};
                }
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    ++m_diskReadyCalls;
                }
                return handledResult(request);
            }

            [[nodiscard]] uint32_t readStreamSectors(const StreamPacket &packet)
            {
                if (packet.sectors == 0u ||
                    packet.sectors > kMaxReadSectors ||
                    packet.destination == 0u)
                {
                    return 0u;
                }

                uint32_t currentLsn = 0u;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (!m_initialized || !m_streaming || m_paused)
                    {
                        return 0u;
                    }
                    currentLsn = m_currentLsn;
                }

                const uint64_t totalBytes =
                    static_cast<uint64_t>(packet.sectors) * kSectorBytes;
                const uint64_t destinationEnd =
                    static_cast<uint64_t>(packet.destination) + totalBytes;
                const uint64_t lsnEnd =
                    static_cast<uint64_t>(currentLsn) + packet.sectors;
                if (destinationEnd > std::numeric_limits<uint32_t>::max() + 1ull ||
                    lsnEnd > std::numeric_limits<uint32_t>::max() + 1ull)
                {
                    return 0u;
                }
                uint32_t normalizedStart = 0u;
                uint32_t normalizedLast = 0u;
                if (!m_host.normalizeGuestAddress(packet.destination,
                                                  normalizedStart) ||
                    !m_host.normalizeGuestAddress(
                        static_cast<uint32_t>(destinationEnd - 1u),
                        normalizedLast) ||
                    normalizedLast < normalizedStart ||
                    static_cast<uint64_t>(normalizedLast - normalizedStart) + 1u !=
                        totalBytes)
                {
                    return 0u;
                }

                std::vector<uint8_t> chunk(kChunkSectors * kSectorBytes);
                uint32_t completed = 0u;
                while (completed < packet.sectors)
                {
                    const uint32_t requested =
                        std::min(kChunkSectors, packet.sectors - completed);
                    uint32_t actual = 0u;
                    if (!m_host.readCdSectors(currentLsn + completed,
                                              requested,
                                              chunk.data(),
                                              chunk.size(),
                                              actual) ||
                        actual == 0u || actual > requested)
                    {
                        break;
                    }

                    const size_t bytes = static_cast<size_t>(actual) * kSectorBytes;
                    const uint32_t destination =
                        packet.destination + completed * kSectorBytes;
                    if (!m_host.writeGuest(destination, chunk.data(), bytes))
                    {
                        break;
                    }
                    completed += actual;
                    if (actual < requested)
                    {
                        break;
                    }
                }

                if (completed != 0u)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_currentLsn += completed;
                    ++m_readCalls;
                    m_sectorsRead += completed;
                }
                return completed;
            }

            [[nodiscard]] RpcResult handleStream(const RpcRequest &request)
            {
                if (request.send.address == 0u ||
                    request.send.size != kStreamPacketBytes ||
                    request.receive.address == 0u ||
                    request.receive.size < sizeof(uint32_t))
                {
                    return {};
                }

                StreamPacket packet{};
                if (!m_host.readGuest(request.send.address,
                                      &packet,
                                      sizeof(packet)))
                {
                    return {};
                }

                uint32_t response = 0u;
                const auto command = static_cast<StreamCommand>(packet.command);
                if (command == StreamCommand::Read)
                {
                    response = readStreamSectors(packet) & 0xFFFFu;
                }
                else
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    switch (command)
                    {
                    case StreamCommand::Init:
                        m_initialized = true;
                        m_streaming = false;
                        m_paused = false;
                        m_currentLsn = 0u;
                        response = 1u;
                        break;
                    case StreamCommand::Start:
                        if (m_initialized)
                        {
                            m_currentLsn = packet.lsn;
                            m_streaming = true;
                            m_paused = false;
                            response = 1u;
                        }
                        break;
                    case StreamCommand::Stop:
                        m_streaming = false;
                        m_paused = false;
                        response = 1u;
                        break;
                    case StreamCommand::Seek:
                    case StreamCommand::SeekForward:
                        if (m_initialized)
                        {
                            m_currentLsn = packet.lsn;
                            response = 1u;
                        }
                        break;
                    case StreamCommand::Stat:
                        response = m_streaming && !m_paused ? 1u : 0u;
                        break;
                    case StreamCommand::Pause:
                        if (m_streaming)
                        {
                            m_paused = true;
                            response = 1u;
                        }
                        break;
                    case StreamCommand::Resume:
                        if (m_streaming)
                        {
                            m_paused = false;
                            response = 1u;
                        }
                        break;
                    default:
                        return {};
                    }
                }

                if (!m_host.writeGuest(request.receive.address,
                                       &response,
                                       sizeof(response)))
                {
                    return {};
                }
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    ++m_streamCalls;
                }
                return handledResult(request);
            }

            IopHost &m_host;
            mutable std::mutex m_mutex;
            bool m_initialized = false;
            bool m_streaming = false;
            bool m_paused = false;
            uint32_t m_currentLsn = 0u;
            uint64_t m_streamCalls = 0u;
            uint64_t m_readCalls = 0u;
            uint64_t m_sectorsRead = 0u;
            uint64_t m_diskReadyCalls = 0u;
            inline static constexpr std::array<uint32_t, 1> kSids{kSid};
        };
    }

    std::unique_ptr<IopService> createCdvdNcmdService(IopHost &host)
    {
        return std::make_unique<CdvdNcmdService>(host);
    }
}
