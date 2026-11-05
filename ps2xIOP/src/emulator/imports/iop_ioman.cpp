#include "iop_ioman.h"

#include "../core/iop_cpu.h"
#include "../core/iop_memory.h"
#include "../services/iop_rpc.h"

#include <algorithm>

namespace ps2x::iop::detail
{
    IopIoman::IopIoman(IopHost &host, IopMemory &memory) noexcept
        : m_host(host), m_memory(memory)
    {
    }

    void IopIoman::reset()
    {
        for (const auto &[fd, file] : m_openFiles)
        {
            (void)fd;
            m_host.closeHostFile(file.hostHandle);
        }
        m_openFiles.clear();
        m_devices.clear();
    }

    bool IopIoman::dispatchImport(uint16_t ordinal, IopCpuState &cpu, IopGuestExecutor &executor)
    {
        constexpr size_t kMaxDevices = 16u;
        const uint32_t a0 = cpu.gpr[4];
        const auto setV0 = [&](uint32_t value)
        {
            cpu.gpr[2] = value;
        };

        switch (ordinal)
        {
        case 4: // open
        {
            constexpr uint32_t kAccessModeMask = 0x0003u;
            constexpr uint32_t kReadOnly = 0x0001u;
            constexpr uint32_t kCreate = 0x0200u;
            constexpr uint32_t kTruncate = 0x0400u;
            constexpr uint32_t kExclusive = 0x0800u;

            const std::string guestPath = m_memory.readString(a0, 1024u);
            const uint32_t mode = cpu.gpr[5];
            if (guestPath.empty() ||
                (mode & kAccessModeMask) != kReadOnly ||
                (mode & (kCreate | kTruncate | kExclusive)) != 0u)
            {
                setV0(0xFFFFFFFFu);
                return true;
            }

            const std::string hostPath = m_host.translateGuestPath(guestPath);
            if (hostPath.empty())
            {
                setV0(0xFFFFFFFFu);
                return true;
            }

            const uint64_t handle = m_host.openHostFile(hostPath);
            if (handle == 0u)
            {
                setV0(0xFFFFFFFFu);
                return true;
            }

            uint64_t size = 0u;
            if (!m_host.hostFileSize(handle, size))
            {
                m_host.closeHostFile(handle);
                setV0(0xFFFFFFFFu);
                return true;
            }

            int32_t fd = -1;
            for (int32_t candidate = 3; candidate < 256; ++candidate)
            {
                if (m_openFiles.find(candidate) == m_openFiles.end())
                {
                    fd = candidate;
                    break;
                }
            }
            if (fd < 0)
            {
                m_host.closeHostFile(handle);
                setV0(0xFFFFFFFFu);
                return true;
            }

            m_openFiles.emplace(fd, OpenFile{handle, 0u, size});
            setV0(static_cast<uint32_t>(fd));
            return true;
        }
        case 5: // close
        {
            const int32_t fd = static_cast<int32_t>(a0);
            const auto file = m_openFiles.find(fd);
            if (file == m_openFiles.end())
            {
                setV0(0xFFFFFFFFu);
                return true;
            }

            m_host.closeHostFile(file->second.hostHandle);
            m_openFiles.erase(file);
            setV0(0u);
            return true;
        }
        case 6: // read
        {
            const int32_t fd = static_cast<int32_t>(a0);
            const auto file = m_openFiles.find(fd);
            const uint32_t destination = cpu.gpr[5];
            const uint32_t requested = cpu.gpr[6];
            if (file == m_openFiles.end() || requested > IopMemory::RamSize)
            {
                setV0(0xFFFFFFFFu);
                return true;
            }
            if (requested == 0u)
            {
                setV0(0u);
                return true;
            }

            std::vector<uint8_t> buffer(requested);
            size_t bytesRead = 0u;
            if (!m_host.readHostFile(file->second.hostHandle,
                                     file->second.offset,
                                     buffer.data(),
                                     buffer.size(),
                                     bytesRead) ||
                bytesRead > buffer.size() ||
                !m_memory.writeRam(destination, buffer.data(), bytesRead))
            {
                setV0(0xFFFFFFFFu);
                return true;
            }

            file->second.offset += bytesRead;
            setV0(static_cast<uint32_t>(bytesRead));
            return true;
        }
        case 8: // lseek
        {
            const int32_t fd = static_cast<int32_t>(a0);
            const auto file = m_openFiles.find(fd);
            if (file == m_openFiles.end())
            {
                setV0(0xFFFFFFFFu);
                return true;
            }

            const int32_t offset = static_cast<int32_t>(cpu.gpr[5]);
            const uint32_t whence = cpu.gpr[6];
            int64_t base = 0;
            switch (whence)
            {
            case 0u: // SEEK_SET
                base = 0;
                break;
            case 1u: // SEEK_CUR
                base = static_cast<int64_t>(file->second.offset);
                break;
            case 2u: // SEEK_END
                base = static_cast<int64_t>(file->second.size);
                break;
            default:
                setV0(0xFFFFFFFFu);
                return true;
            }

            const int64_t newOffset = base + static_cast<int64_t>(offset);
            if (newOffset < 0 || newOffset > 0x7FFFFFFFLL)
            {
                setV0(0xFFFFFFFFu);
                return true;
            }

            file->second.offset = static_cast<uint64_t>(newOffset);
            setV0(static_cast<uint32_t>(newOffset));
            return true;
        }
        case 20: // AddDrv
        {
            if (a0 == 0u || m_devices.size() >= kMaxDevices)
            {
                setV0(0xFFFFFFFFu);
                return true;
            }

            const uint32_t nameAddress = m_memory.read32(a0);
            const uint32_t operations = m_memory.read32(a0 + 16u);
            const std::string name = m_memory.readString(nameAddress, 64u);
            if (nameAddress == 0u || operations == 0u || name.empty())
            {
                setV0(0xFFFFFFFFu);
                return true;
            }

            m_devices.push_back({a0, cpu.gpr[28], name});
            const uint32_t init = m_memory.read32(operations);
            if (init != 0u)
            {
                const int32_t result = static_cast<int32_t>(
                    executor.executeGuestFunction(init, a0, 0u, 0u, 0u, cpu.gpr[28]));
                if (result < 0)
                {
                    m_devices.pop_back();
                    setV0(0xFFFFFFFFu);
                    return true;
                }
            }

            setV0(0u);
            return true;
        }
        case 21: // DelDrv
        {
            const std::string name = m_memory.readString(a0, 64u);
            const auto device = std::find_if(
                m_devices.begin(), m_devices.end(),
                [&](const Device &candidate)
                { return candidate.name == name; });
            if (device == m_devices.end())
            {
                setV0(0xFFFFFFFFu);
                return true;
            }

            const uint32_t operations = m_memory.read32(device->address + 16u);
            const uint32_t deinit = operations != 0u ? m_memory.read32(operations + 4u) : 0u;
            if (deinit != 0u)
                (void)executor.executeGuestFunction(deinit, device->address, 0u, 0u, 0u, device->gp);
            m_devices.erase(device);
            setV0(0u);
            return true;
        }
        default:
            return false;
        }
    }
}
