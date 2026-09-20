#include "Common.h"
#include "FileIO.h"

namespace ps2_syscalls
{
    static PS2VfsMounts currentVfsMounts()
    {
        const PS2Runtime::IoPaths &paths = PS2Runtime::getIoPaths();
        return {paths.hostRoot, paths.cdRoot, paths.mcRoot};
    }

    struct VagAccumEntry
    {
        std::vector<uint8_t> data;
        uint32_t firstBufAddr = 0;
    };
    static std::unordered_map<int, VagAccumEntry> g_vagAccum;
    static std::mutex g_vagAccumMutex;
    static constexpr size_t kVagAccumMaxBytes = 16 * 1024 * 1024;

    void fioOpen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4); // $a0
        int flags = (int)getRegU32(ctx, 5);    // $a1 (PS2 FIO flags)

        const char *ps2Path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        if (!ps2Path)
        {
            std::cerr << "fioOpen error: Invalid path address" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        if (!runtime)
        {
            setReturnS32(ctx, -1);
            return;
        }

        const int32_t descriptor = runtime->vfs().open(ps2Path, static_cast<uint32_t>(flags), currentVfsMounts(), runtime->romDevice());
        setReturnS32(ctx, descriptor);
    }

    void fioClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int ps2Fd = (int)getRegU32(ctx, 4);

        if (!runtime)
        {
            setReturnS32(ctx, -1);
            return;
        }

        const int32_t ret = runtime->vfs().close(ps2Fd);
        if (ret < 0)
        {
            setReturnS32(ctx, -1);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(g_vagAccumMutex);
            auto it = g_vagAccum.find(ps2Fd);
            if (it != g_vagAccum.end())
            {
                VagAccumEntry &e = it->second;
                if (e.data.size() >= 48)
                {
                    const uint32_t magic = (static_cast<uint32_t>(e.data[0]) << 24) |
                                           (static_cast<uint32_t>(e.data[1]) << 16) |
                                           (static_cast<uint32_t>(e.data[2]) << 8) |
                                           static_cast<uint32_t>(e.data[3]);
                    const uint32_t magicLE = (static_cast<uint32_t>(e.data[3]) << 24) |
                                             (static_cast<uint32_t>(e.data[2]) << 16) |
                                             (static_cast<uint32_t>(e.data[1]) << 8) |
                                             static_cast<uint32_t>(e.data[0]);
                    if (magic == 0x56414770u || magicLE == 0x56414770u)
                    {
                        if (runtime)
                            runtime->audioBackend().onVagTransferFromBuffer(
                                e.data.data(), static_cast<uint32_t>(e.data.size()),
                                e.firstBufAddr ? e.firstBufAddr : 0u);
                    }
                }
                g_vagAccum.erase(it);
            }
        }

        setReturnS32(ctx, 0);
    }

    void fioRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int ps2Fd = (int)getRegU32(ctx, 4);   // $a0
        uint32_t bufAddr = getRegU32(ctx, 5); // $a1
        size_t size = getRegU32(ctx, 6);      // $a2

        uint8_t *hostBuf = getMemPtr(rdram, bufAddr);
        if (!hostBuf)
        {
            std::cerr << "fioRead error: Invalid buffer address for fd " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EFAULT
            return;
        }
        if (!runtime)
        {
            std::cerr << "fioRead error: Invalid file descriptor " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EBADF
            return;
        }
        if (size == 0)
        {
            setReturnS32(ctx, 0); // Read 0 bytes
            return;
        }

        const int64_t readResult = runtime->vfs().read(ps2Fd, hostBuf, size);
        if (readResult < 0)
        {
            setReturnS32(ctx, -1);
            return;
        }
        const size_t bytesRead = static_cast<size_t>(readResult);
        if (bytesRead > 0)
        {
            ps2TraceGuestRangeWrite(rdram, bufAddr, static_cast<uint32_t>(bytesRead), "fioRead", ctx);
        }

        {
            std::lock_guard<std::mutex> lock(g_vagAccumMutex);
            auto it = g_vagAccum.find(ps2Fd);
            if (it != g_vagAccum.end())
            {
                VagAccumEntry &e = it->second;
                if (e.data.size() + bytesRead <= kVagAccumMaxBytes)
                    e.data.insert(e.data.end(), hostBuf, hostBuf + bytesRead);
            }
            else if (bytesRead >= 4)
            {
                const uint32_t magic = (static_cast<uint32_t>(hostBuf[0]) << 24) |
                                       (static_cast<uint32_t>(hostBuf[1]) << 16) |
                                       (static_cast<uint32_t>(hostBuf[2]) << 8) |
                                       static_cast<uint32_t>(hostBuf[3]);
                const uint32_t magicLE = (static_cast<uint32_t>(hostBuf[3]) << 24) |
                                         (static_cast<uint32_t>(hostBuf[2]) << 16) |
                                         (static_cast<uint32_t>(hostBuf[1]) << 8) |
                                         static_cast<uint32_t>(hostBuf[0]);
                if (magic == 0x56414770u || magicLE == 0x56414770u)
                {
                    VagAccumEntry &e = g_vagAccum[ps2Fd];
                    e.firstBufAddr = bufAddr;
                    if (bytesRead <= kVagAccumMaxBytes)
                        e.data.assign(hostBuf, hostBuf + bytesRead);
                }
            }
        }

        setReturnS32(ctx, (int32_t)bytesRead);
    }

    void fioWrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int ps2Fd = (int)getRegU32(ctx, 4);   // $a0
        uint32_t bufAddr = getRegU32(ctx, 5); // $a1
        size_t size = getRegU32(ctx, 6);      // $a2

        const uint8_t *hostBuf = getConstMemPtr(rdram, bufAddr);
        if (!hostBuf)
        {
            setReturnS32(ctx, -1);
            return;
        }

        if (!runtime)
        {
            setReturnS32(ctx, -1); // -EFAULT
            return;
        }

        if (size == 0)
        {
            setReturnS32(ctx, 0); // Wrote 0 bytes
            return;
        }

        const int64_t writeResult = runtime->vfs().write(ps2Fd, hostBuf, size);
        if (writeResult < 0)
        {
            setReturnS32(ctx, -1);
            return;
        }

        // returns number of bytes written
        setReturnS32(ctx, static_cast<int32_t>(writeResult));
    }

    void fioLseek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int ps2Fd = (int)getRegU32(ctx, 4);  // $a0
        int32_t offset = getRegU32(ctx, 5);  // $a1 (PS2 seems to use 32-bit offset here commonly)
        int whence = (int)getRegU32(ctx, 6); // $a2 (PS2 FIO_SEEK constants)

        if (!runtime)
        {
            std::cerr << "fioLseek error: Invalid file descriptor " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EBADF
            return;
        }

        int hostWhence;
        switch (whence)
        {
        case PS2_FIO_SEEK_SET:
            hostWhence = SEEK_SET;
            break;
        case PS2_FIO_SEEK_CUR:
            hostWhence = SEEK_CUR;
            break;
        case PS2_FIO_SEEK_END:
            hostWhence = SEEK_END;
            break;
        default:
            std::cerr << "fioLseek error: Invalid whence value " << whence << " for fd " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EINVAL
            return;
        }

        const int64_t newPos = runtime->vfs().seek(ps2Fd, offset, hostWhence);
        if (newPos < 0)
        {
            setReturnS32(ctx, -1);
        }
        else
        {
            if (static_cast<uint64_t>(newPos) > 0x7FFFFFFFu)
            {
                std::cerr << "fioLseek warning: New position exceeds 32-bit for fd " << ps2Fd << std::endl;
                setReturnS32(ctx, -1);
            }
            else
            {
                setReturnS32(ctx, (int32_t)newPos);
            }
        }
    }

    void fioMkdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4); // $a0
        // int mode = (int)getRegU32(ctx, 5);  // $a1 - ignored on host

        const char *ps2Path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        if (!ps2Path)
        {
            std::cerr << "fioMkdir error: Invalid path address" << std::endl;
            setReturnS32(ctx, -1); // -EFAULT
            return;
        }
        std::filesystem::path hostPath;
        if (!runtime || !runtime->vfs().resolveHostPath(ps2Path, currentVfsMounts(), hostPath))
        {
            std::cerr << "fioMkdir error: Failed to translate path '" << ps2Path << "'" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }
        std::error_code ec;
        bool success = std::filesystem::create_directory(hostPath, ec);

        if (!success && ec)
        {
            std::cerr << "fioMkdir error: create_directory failed for '" << hostPath.string()
                      << "': " << ec.message() << std::endl;
            setReturnS32(ctx, -1);
        }
        else
        {
            RUNTIME_LOG("fioMkdir: Created directory '" << hostPath.string() << "'");
            setReturnS32(ctx, 0); // Success
        }
    }

    void fioChdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4); // $a0
        const char *ps2Path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        if (!ps2Path)
        {
            std::cerr << "fioChdir error: Invalid path address" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        PS2VfsStat status;
        if (!runtime || !runtime->vfs().stat(ps2Path, currentVfsMounts(), runtime->romDevice(), status) || !status.directory)
        {
            setReturnS32(ctx, -1);
        }
        else
        {
            setReturnS32(ctx, 0);
        }
    }

    void fioRmdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4); // $a0
        const char *ps2Path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        if (!ps2Path)
        {
            std::cerr << "fioRmdir error: Invalid path address" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }
        std::filesystem::path hostPath;
        if (!runtime || !runtime->vfs().resolveHostPath(ps2Path, currentVfsMounts(), hostPath))
        {
            std::cerr << "fioRmdir error: Failed to translate path '" << ps2Path << "'" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        std::error_code ec;
        bool success = std::filesystem::remove(hostPath, ec);

        if (!success || ec)
        {
            std::cerr << "fioRmdir error: remove failed for '" << hostPath.string() << "': " << ec.message() << std::endl;
            setReturnS32(ctx, -1);
        }
        else
        {
            RUNTIME_LOG("fioRmdir: Removed directory '" << hostPath.string() << "'");
            setReturnS32(ctx, 0); // Success
        }
    }

    void fioGetstat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4);    // $a0
        uint32_t statBufAddr = getRegU32(ctx, 5); // $a1

        const char *ps2Path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        uint8_t *ps2StatBuf = getMemPtr(rdram, statBufAddr);

        if (!ps2Path)
        {
            std::cerr << "fioGetstat error: Invalid path addr" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }
        if (!ps2StatBuf)
        {
            std::cerr << "fioGetstat error: Invalid buffer addr" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        if (!runtime)
        {
            setReturnS32(ctx, -1);
            return;
        }

        PS2VfsStat status;
        if (!runtime->vfs().stat(ps2Path, currentVfsMounts(), runtime->romDevice(), status))
        {
            setReturnS32(ctx, -1);
            return;
        }

        io_stat_t guest{};
        guest.mode = (status.directory ? kFioSoIfDir : kFioSoIfReg) | kFioSoIROth | kFioSoIXOth | (status.readOnly ? 0u : kFioSoIWOth);
        guest.size = static_cast<uint32_t>(status.size & 0xFFFFFFFFu);
        guest.hisize = static_cast<uint32_t>(status.size >> 32u);
        encodePs2Time(status.created, guest.ctime);
        encodePs2Time(status.accessed, guest.atime);
        encodePs2Time(status.modified, guest.mtime);
        std::memcpy(ps2StatBuf, &guest, sizeof(guest));
        ps2TraceGuestRangeWrite(rdram, statBufAddr, sizeof(guest), "fioGetstat", ctx);
        setReturnS32(ctx, 0);
    }

    void fioRemove(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4); // $a0
        const char *ps2Path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        if (!ps2Path)
        {
            std::cerr << "fioRemove error: Invalid path" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        std::filesystem::path hostPath;
        if (!runtime || !runtime->vfs().resolveHostPath(ps2Path, currentVfsMounts(), hostPath))
        {
            std::cerr << "fioRemove error: Path translate fail" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        std::error_code ec;
        bool success = std::filesystem::remove(hostPath, ec);

        if (!success || ec)
        {
            std::cerr << "fioRemove error: remove failed for '" << hostPath.string() << "': " << ec.message() << std::endl;
            setReturnS32(ctx, -1);
        }
        else
        {
            RUNTIME_LOG("fioRemove: Removed file '" << hostPath.string() << "'");
            setReturnS32(ctx, 0); // Success
        }
    }
}
