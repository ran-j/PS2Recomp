#include "../iop_service.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kMcservSid = 0x80000400u;
        constexpr uint32_t kMcservDev9Sid = 0x80000480u;
        constexpr int32_t kSucceeded = 0;
        constexpr int32_t kDenied = -5;
        constexpr uint32_t kMcservVersion = 0x0205u;
        constexpr uint32_t kMcmanVersion = 0x0206u;
        constexpr uint32_t kCreateDirectory = 0x0040u;

        enum class Operation
        {
            Init,
            GetInfo,
            Open,
            Close,
            Seek,
            Read,
            Write,
            Flush,
            Chdir,
            GetDir,
            SetInfo,
            Delete,
            Format,
            Unformat,
            GetEnt,
            ChangePriority,
            Unknown,
        };

        enum class Flavor
        {
            OldMcserv,
            NewXmcserv,
        };

#pragma pack(push, 1)
        struct DescriptorParameter
        {
            int32_t fd;
            int32_t port;
            int32_t slot;
            int32_t size;
            int32_t offset;
            int32_t origin;
            uint32_t buffer;
            uint32_t parameter;
            uint8_t data[16];
        };

        struct NameParameter
        {
            int32_t port;
            int32_t slot;
            int32_t flags;
            int32_t maxEntries;
            uint32_t pointer;
            char name[1024];
        };
#pragma pack(pop)

        static_assert(sizeof(DescriptorParameter) == 48u);
        static_assert(sizeof(NameParameter) == 1044u);

        Operation decodeOperation(uint32_t function, Flavor &flavor)
        {
            flavor = Flavor::NewXmcserv;
            switch (function)
            {
            case 0xFEu: return Operation::Init;
            case 0x01u: return Operation::GetInfo;
            case 0x02u: return Operation::Open;
            case 0x03u: return Operation::Close;
            case 0x04u: return Operation::Seek;
            case 0x05u: return Operation::Read;
            case 0x06u: return Operation::Write;
            case 0x0Au: return Operation::Flush;
            case 0x0Cu: return Operation::Chdir;
            case 0x0Du: return Operation::GetDir;
            case 0x0Eu: return Operation::SetInfo;
            case 0x0Fu: return Operation::Delete;
            case 0x10u: return Operation::Format;
            case 0x11u: return Operation::Unformat;
            case 0x12u: return Operation::GetEnt;
            case 0x14u: return Operation::ChangePriority;
            default: break;
            }

            flavor = Flavor::OldMcserv;
            switch (function)
            {
            case 0x70u: return Operation::Init;
            case 0x71u: return Operation::Open;
            case 0x72u: return Operation::Close;
            case 0x73u: return Operation::Read;
            case 0x74u: return Operation::Write;
            case 0x75u: return Operation::Seek;
            case 0x76u: return Operation::GetDir;
            case 0x77u: return Operation::Format;
            case 0x78u: return Operation::GetInfo;
            case 0x79u: return Operation::Delete;
            case 0x7Au: return Operation::Flush;
            case 0x7Bu: return Operation::Chdir;
            case 0x7Cu: return Operation::SetInfo;
            case 0x80u: return Operation::Unformat;
            default: return Operation::Unknown;
            }
        }

        bool usesNameParameter(Operation operation)
        {
            return operation == Operation::Open || operation == Operation::Chdir ||
                   operation == Operation::GetDir || operation == Operation::SetInfo ||
                   operation == Operation::Delete || operation == Operation::GetEnt;
        }

        class McservService final : public IopService
        {
        public:
            explicit McservService(IopHost &host) : m_host(host) {}

            [[nodiscard]] std::string_view name() const override { return "MCSERV"; }
            [[nodiscard]] std::span<const uint32_t> sids() const override { return m_sids; }

            void reset() override
            {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_unknownRpcLogCount = 0u;
                }
                (void)call(MemoryCardOperation::Init);
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                RpcResult response{};
                response.handled = true;
                response.resultAddress = request.receive.address;

                Flavor flavor = Flavor::NewXmcserv;
                const Operation operation = decodeOperation(request.function, flavor);
                if (operation == Operation::Init)
                {
                    (void)call(MemoryCardOperation::Init);
                    writeInitResult(request.receive);
                    return response;
                }

                int32_t result = kDenied;
                if (operation == Operation::Unknown)
                {
                    logUnknown(request);
                    writeResult(request.receive, result);
                    return response;
                }

                if (usesNameParameter(operation))
                {
                    NameParameter parameter{};
                    if (request.send.address != 0u &&
                        request.send.size >= offsetof(NameParameter, name) &&
                        m_host.readGuest(request.send.address, &parameter, sizeof(parameter)))
                    {
                        result = handleNameOperation(operation, request.send.address, parameter);
                    }
                }
                else
                {
                    DescriptorParameter parameter{};
                    if (request.send.address != 0u &&
                        request.send.size >= sizeof(parameter) &&
                        m_host.readGuest(request.send.address, &parameter, sizeof(parameter)))
                    {
                        if (operation == Operation::Write && parameter.origin > 0 &&
                            parameter.origin <= static_cast<int32_t>(sizeof(parameter.data)))
                        {
                            const uint32_t inlineAddress =
                                request.send.address + static_cast<uint32_t>(offsetof(DescriptorParameter, data));
                            const int32_t prefix = call(MemoryCardOperation::Write,
                                                        static_cast<uint32_t>(parameter.fd),
                                                        inlineAddress,
                                                        static_cast<uint32_t>(parameter.origin));
                            if (prefix < 0)
                            {
                                result = prefix;
                            }
                            else
                            {
                                const int32_t body = call(MemoryCardOperation::Write,
                                                          static_cast<uint32_t>(parameter.fd),
                                                          parameter.buffer,
                                                          static_cast<uint32_t>(std::max(parameter.size, 0)));
                                result = body < 0 ? body : prefix + body;
                            }
                        }
                        else
                        {
                            result = handleDescriptorOperation(operation, flavor, parameter);
                        }
                    }
                }

                writeResult(request.receive, result);
                return response;
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                metrics.push_back({"unknown_rpc_logs", m_unknownRpcLogCount, false});
            }

        private:
            int32_t call(MemoryCardOperation operation,
                         uint32_t a0 = 0u,
                         uint32_t a1 = 0u,
                         uint32_t a2 = 0u,
                         uint32_t a3 = 0u,
                         uint32_t stackArgument = 0u)
            {
                return m_host.memoryCard({operation, {a0, a1, a2, a3, stackArgument}});
            }

            void writeResult(GuestBuffer receive, int32_t result)
            {
                if (receive.address == 0u || receive.size < sizeof(result))
                {
                    return;
                }
                (void)m_host.writeGuest(receive.address, &result, sizeof(result));
                if (receive.size > sizeof(result))
                {
                    (void)m_host.zeroGuest(receive.address + sizeof(result),
                                           receive.size - sizeof(result));
                }
            }

            void writeInitResult(GuestBuffer receive)
            {
                if (receive.address == 0u || receive.size < sizeof(int32_t))
                {
                    return;
                }
                const std::array<uint32_t, 3> values = {
                    static_cast<uint32_t>(kSucceeded), kMcservVersion, kMcmanVersion};
                const uint32_t bytes = std::min<uint32_t>(receive.size, sizeof(values));
                (void)m_host.writeGuest(receive.address, values.data(), bytes);
                if (receive.size > bytes)
                {
                    (void)m_host.zeroGuest(receive.address + bytes, receive.size - bytes);
                }
            }

            int32_t handleNameOperation(Operation operation,
                                        uint32_t sendAddress,
                                        const NameParameter &parameter)
            {
                const uint32_t nameAddress =
                    sendAddress + static_cast<uint32_t>(offsetof(NameParameter, name));
                const uint32_t port = static_cast<uint32_t>(parameter.port);
                const uint32_t slot = static_cast<uint32_t>(parameter.slot);
                switch (operation)
                {
                case Operation::Open:
                    if ((static_cast<uint32_t>(parameter.flags) & kCreateDirectory) != 0u)
                    {
                        return call(MemoryCardOperation::Mkdir, port, slot, nameAddress);
                    }
                    return call(MemoryCardOperation::Open,
                                port, slot, nameAddress,
                                static_cast<uint32_t>(parameter.flags));
                case Operation::Chdir:
                    return call(MemoryCardOperation::Chdir,
                                port, slot, nameAddress, parameter.pointer);
                case Operation::SetInfo:
                    return call(MemoryCardOperation::SetFileInfo, port, slot, nameAddress);
                case Operation::Delete:
                    return call(MemoryCardOperation::Delete, port, slot, nameAddress);
                case Operation::GetDir:
                    // MCSERV normally DMA-writes entries. The existing HLE returns
                    // zero entries instead of fabricating directory contents.
                    return kSucceeded;
                case Operation::GetEnt:
                    return 1024;
                default:
                    return kDenied;
                }
            }

            int32_t handleDescriptorOperation(Operation operation,
                                               Flavor flavor,
                                               const DescriptorParameter &parameter)
            {
                switch (operation)
                {
                case Operation::GetInfo:
                {
                    uint32_t typeAddress = 0u;
                    uint32_t freeAddress = 0u;
                    uint32_t formatAddress = 0u;
                    if (parameter.parameter != 0u)
                    {
                        const uint32_t outputSize = flavor == Flavor::NewXmcserv ? 192u : 64u;
                        (void)m_host.zeroGuest(parameter.parameter, outputSize);
                        typeAddress = parameter.parameter;
                        freeAddress = parameter.parameter + 4u;
                        if (flavor == Flavor::NewXmcserv)
                        {
                            formatAddress = parameter.parameter + 144u;
                        }
                    }
                    return call(MemoryCardOperation::GetInfo,
                                static_cast<uint32_t>(parameter.port),
                                static_cast<uint32_t>(parameter.slot),
                                typeAddress,
                                freeAddress,
                                formatAddress);
                }
                case Operation::Close:
                    return call(MemoryCardOperation::Close, static_cast<uint32_t>(parameter.fd));
                case Operation::Seek:
                    return call(MemoryCardOperation::Seek,
                                static_cast<uint32_t>(parameter.fd),
                                static_cast<uint32_t>(parameter.offset),
                                static_cast<uint32_t>(parameter.origin));
                case Operation::Read:
                    if (parameter.parameter != 0u)
                    {
                        (void)m_host.zeroGuest(parameter.parameter,
                                               flavor == Flavor::NewXmcserv ? 192u : 64u);
                    }
                    return call(MemoryCardOperation::Read,
                                static_cast<uint32_t>(parameter.fd),
                                parameter.buffer,
                                static_cast<uint32_t>(std::max(parameter.size, 0)));
                case Operation::Write:
                    return call(MemoryCardOperation::Write,
                                static_cast<uint32_t>(parameter.fd),
                                parameter.buffer,
                                static_cast<uint32_t>(std::max(parameter.size, 0)));
                case Operation::Flush:
                    return call(MemoryCardOperation::Flush, static_cast<uint32_t>(parameter.fd));
                case Operation::Format:
                    return call(MemoryCardOperation::Format,
                                static_cast<uint32_t>(parameter.port),
                                static_cast<uint32_t>(parameter.slot));
                case Operation::Unformat:
                    return call(MemoryCardOperation::Unformat,
                                static_cast<uint32_t>(parameter.port),
                                static_cast<uint32_t>(parameter.slot));
                case Operation::ChangePriority:
                    return kSucceeded;
                default:
                    return kDenied;
                }
            }

            void logUnknown(const RpcRequest &request)
            {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_unknownRpcLogCount >= 32u)
                    {
                        return;
                    }
                    ++m_unknownRpcLogCount;
                }
                std::ostringstream message;
                message << "MCSERV unknown RPC sid=0x" << std::hex << request.sid
                        << " function=0x" << request.function
                        << " send=0x" << request.send.address
                        << " size=0x" << request.send.size;
                m_host.log(LogLevel::Warning, message.str());
            }

            IopHost &m_host;
            mutable std::mutex m_mutex;
            uint32_t m_unknownRpcLogCount = 0u;
            const std::array<uint32_t, 2> m_sids = {kMcservSid, kMcservDev9Sid};
        };
    }

    std::unique_ptr<IopService> createMcservService(IopHost &host)
    {
        return std::make_unique<McservService>(host);
    }
}
