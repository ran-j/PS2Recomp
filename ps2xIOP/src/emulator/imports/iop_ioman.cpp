#include "iop_ioman.h"

#include "../core/iop_cpu.h"
#include "../core/iop_memory.h"
#include "../services/iop_rpc.h"

#include <algorithm>
#include <vector>

namespace ps2x::iop::detail
{
    IopIoman::IopIoman(IopHost &host, IopMemory &memory) noexcept
        : m_host(host), m_memory(memory)
    {
    }

    void IopIoman::reset()
    {
        for (auto &[fd, file] : m_files)
        {
            (void)fd;
            m_host.closeHostFile(file.handle);
        }
        m_files.clear();
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
        case 5: // open(path, mode) -> fd (XAVSTRM.IRX imports 5/6/8; a real
                // fd with host backing, never the old unhandled v0=0 alias)
        {
            const std::string guestPath = m_memory.readString(a0, 512u);
            if (guestPath.empty())
            {
                setV0(0xFFFFFFFFu);
                return true;
            }
            std::string hostPath = m_host.translateGuestPath(guestPath);
            if (hostPath.empty())
                hostPath = guestPath;
            const uint64_t handle = m_host.openHostFile(hostPath);
            if (handle == 0u)
            {
                setV0(0xFFFFFFFFu);
                return true;
            }
            int32_t fd = 0;
            while (m_files.find(fd) != m_files.end())
                ++fd;
            m_files[fd] = {handle, 0u};
            setV0(static_cast<uint32_t>(fd));
            return true;
        }
        case 6: // close(fd)
        {
            const auto it = m_files.find(static_cast<int32_t>(a0));
            if (it == m_files.end())
            {
                setV0(0xFFFFFFFFu);
                return true;
            }
            m_host.closeHostFile(it->second.handle);
            m_files.erase(it);
            setV0(0u);
            return true;
        }
        case 8: // read(fd, buf, size) -> bytes read (0 at EOF, -1 on error)
        {
            const uint32_t buffer = cpu.gpr[5];
            const uint32_t size = cpu.gpr[6];
            const auto it = m_files.find(static_cast<int32_t>(a0));
            if (it == m_files.end() || (buffer == 0u && size != 0u))
            {
                setV0(0xFFFFFFFFu);
                return true;
            }
            if (size == 0u)
            {
                setV0(0u);
                return true;
            }
            std::vector<uint8_t> data(size);
            size_t bytesRead = 0u;
            if (!m_host.readHostFile(it->second.handle, it->second.offset, data.data(), data.size(), bytesRead))
            {
                setV0(0xFFFFFFFFu);
                return true;
            }
            if (bytesRead != 0u && !m_memory.writeRam(buffer, data.data(), bytesRead))
            {
                setV0(0xFFFFFFFFu);
                return true;
            }
            it->second.offset += bytesRead;
            setV0(static_cast<uint32_t>(bytesRead));
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
