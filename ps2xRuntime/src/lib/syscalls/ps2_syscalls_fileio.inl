static int allocatePs2Fd(FILE *file)
{
    if (!file)
        return -1;

    std::lock_guard<std::mutex> lock(g_fd_mutex);
    int fd = g_nextFd++;
    g_fileDescriptors[fd] = file;
    return fd;
}

static FILE *getHostFile(int ps2Fd)
{
    std::lock_guard<std::mutex> lock(g_fd_mutex);
    auto it = g_fileDescriptors.find(ps2Fd);
    if (it != g_fileDescriptors.end())
    {
        return it->second;
    }
    return nullptr;
}

static void releasePs2Fd(int ps2Fd)
{
    std::lock_guard<std::mutex> lock(g_fd_mutex);
    g_fileDescriptors.erase(ps2Fd);
}

static const char *translateFioMode(int ps2Flags)
{
    bool read = (ps2Flags & PS2_FIO_O_RDONLY) || (ps2Flags & PS2_FIO_O_RDWR);
    bool write = (ps2Flags & PS2_FIO_O_WRONLY) || (ps2Flags & PS2_FIO_O_RDWR);
    bool append = (ps2Flags & PS2_FIO_O_APPEND);
    bool create = (ps2Flags & PS2_FIO_O_CREAT);
    bool truncate = (ps2Flags & PS2_FIO_O_TRUNC);

    if (read && write)
    {
        if (create && truncate)
            return "w+b";
        if (create)
            return "a+b";
        return "r+b";
    }
    else if (write)
    {
        if (append)
            return "ab";
        if (create && truncate)
            return "wb";
        if (create)
            return "wx";
        return "r+b";
    }
    else if (read)
    {
        return "rb";
    }
    return "rb";
}

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

    std::string hostPath = translatePs2Path(ps2Path);
    if (hostPath.empty())
    {
        std::cerr << "fioOpen error: Failed to translate path '" << ps2Path << "'" << std::endl;
        setReturnS32(ctx, -1);
        return;
    }

    const char *mode = translateFioMode(flags);
    std::cout << "fioOpen: '" << hostPath << "' flags=0x" << std::hex << flags << std::dec << " mode='" << mode << "'" << std::endl;

    FILE *fp = ::fopen(hostPath.c_str(), mode);
    if (!fp)
    {
        std::cerr << "fioOpen error: fopen failed for '" << hostPath << "': " << strerror(errno) << std::endl;
        setReturnS32(ctx, -1); // e.g., -ENOENT, -EACCES
        return;
    }

    int ps2Fd = allocatePs2Fd(fp);
    if (ps2Fd < 0)
    {
        std::cerr << "fioOpen error: Failed to allocate PS2 file descriptor" << std::endl;
        ::fclose(fp);
        setReturnS32(ctx, -1); // e.g., -EMFILE
        return;
    }

    // returns the PS2 file descriptor
    setReturnS32(ctx, ps2Fd);
}

void fioClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    int ps2Fd = (int)getRegU32(ctx, 4); // $a0
    std::cout << "fioClose: fd=" << ps2Fd << std::endl;

    FILE *fp = getHostFile(ps2Fd);
    if (!fp)
    {
        std::cerr << "fioClose warning: Invalid PS2 file descriptor " << ps2Fd << std::endl;
        setReturnS32(ctx, -1); // e.g., -EBADF
        return;
    }

    int ret = ::fclose(fp);
    releasePs2Fd(ps2Fd);

    // returns 0 on success, -1 on error
    setReturnS32(ctx, ret == 0 ? 0 : -1);
}

void fioRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    int ps2Fd = (int)getRegU32(ctx, 4);   // $a0
    uint32_t bufAddr = getRegU32(ctx, 5); // $a1
    size_t size = getRegU32(ctx, 6);      // $a2

    uint8_t *hostBuf = getMemPtr(rdram, bufAddr);
    FILE *fp = getHostFile(ps2Fd);

    if (!hostBuf)
    {
        std::cerr << "fioRead error: Invalid buffer address for fd " << ps2Fd << std::endl;
        setReturnS32(ctx, -1); // -EFAULT
        return;
    }
    if (!fp)
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

    size_t bytesRead = 0;
    {
        std::lock_guard<std::mutex> lock(g_sys_fd_mutex);
        bytesRead = fread(hostBuf, 1, size, fp);
    }

    if (bytesRead < size && ferror(fp))
    {
        std::cerr << "fioRead error: fread failed for fd " << ps2Fd << ": " << strerror(errno) << std::endl;
        clearerr(fp);
        setReturnS32(ctx, -1); // -EIO or other appropriate error
        return;
    }

    // returns number of bytes read (can be 0 for EOF)
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

    size_t bytesWritten = 0;
    {
        std::lock_guard<std::mutex> lock(g_fd_mutex);
        FILE *fp = getHostFile(ps2Fd);
        if (!fp)
        {
            setReturnS32(ctx, -1); // -EFAULT
            return;
        }

        if (size == 0)
        {
            setReturnS32(ctx, 0); // Wrote 0 bytes
            return;
        }

        bytesWritten = ::fwrite(hostBuf, 1, size, fp);
        if (bytesWritten < size && ferror(fp))
        {
            clearerr(fp);
            setReturnS32(ctx, -1); // -EIO, -ENOSPC etc.
            return;
        }
    }

    // returns number of bytes written
    setReturnS32(ctx, (int32_t)bytesWritten);
}

void fioLseek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    int ps2Fd = (int)getRegU32(ctx, 4);  // $a0
    int32_t offset = getRegU32(ctx, 5);  // $a1 (PS2 seems to use 32-bit offset here commonly)
    int whence = (int)getRegU32(ctx, 6); // $a2 (PS2 FIO_SEEK constants)

    FILE *fp = getHostFile(ps2Fd);
    if (!fp)
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

    if (::fseek(fp, static_cast<long>(offset), hostWhence) != 0)
    {
        std::cerr << "fioLseek error: fseek failed for fd " << ps2Fd << ": " << strerror(errno) << std::endl;
        setReturnS32(ctx, -1); // Return error code
        return;
    }

    long newPos = ::ftell(fp);
    if (newPos < 0)
    {
        std::cerr << "fioLseek error: ftell failed after fseek for fd " << ps2Fd << ": " << strerror(errno) << std::endl;
        setReturnS32(ctx, -1);
    }
    else
    {
        if (newPos > 0xFFFFFFFFL)
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
    // int mode = (int)getRegU32(ctx, 5);

    const char *ps2Path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
    if (!ps2Path)
    {
        std::cerr << "fioMkdir error: Invalid path address" << std::endl;
        setReturnS32(ctx, -1); // -EFAULT
        return;
    }
    std::string hostPath = translatePs2Path(ps2Path);
    if (hostPath.empty())
    {
        std::cerr << "fioMkdir error: Failed to translate path '" << ps2Path << "'" << std::endl;
        setReturnS32(ctx, -1);
        return;
    }

#ifdef _WIN32
    int ret = -1;
#else
    int ret = ::mkdir(hostPath.c_str(), 0775);
#endif

    if (ret != 0)
    {
        std::cerr << "fioMkdir error: mkdir failed for '" << hostPath << "': " << strerror(errno) << std::endl;
        setReturnS32(ctx, -1); // errno
    }
    else
    {
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

    std::string hostPath = translatePs2Path(ps2Path);
    if (hostPath.empty())
    {
        std::cerr << "fioChdir error: Failed to translate path '" << ps2Path << "'" << std::endl;
        setReturnS32(ctx, -1);
        return;
    }

    std::cerr << "fioChdir: Attempting host chdir to '" << hostPath << "' (Stub - Check side effects)" << std::endl;

#ifdef _WIN32
    int ret = -1;
#else
    int ret = ::chdir(hostPath.c_str());
#endif

    if (ret != 0)
    {
        std::cerr << "fioChdir error: chdir failed for '" << hostPath << "': " << strerror(errno) << std::endl;
        setReturnS32(ctx, -1);
    }
    else
    {
        setReturnS32(ctx, 0); // Success
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
    std::string hostPath = translatePs2Path(ps2Path);
    if (hostPath.empty())
    {
        std::cerr << "fioRmdir error: Failed to translate path '" << ps2Path << "'" << std::endl;
        setReturnS32(ctx, -1);
        return;
    }

#ifdef _WIN32
    int ret = -1;
#else
    int ret = ::rmdir(hostPath.c_str());
#endif

    if (ret != 0)
    {
        std::cerr << "fioRmdir error: rmdir failed for '" << hostPath << "': " << strerror(errno) << std::endl;
        setReturnS32(ctx, -1);
    }
    else
    {
        setReturnS32(ctx, 0); // Success
    }
}

void fioGetstat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    // we wont implement this for now.
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

    std::string hostPath = translatePs2Path(ps2Path);
    if (hostPath.empty())
    {
        std::cerr << "fioGetstat error: Bad path translate" << std::endl;
        setReturnS32(ctx, -1);
        return;
    }

    setReturnS32(ctx, -1);
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

    std::string hostPath = translatePs2Path(ps2Path);
    if (hostPath.empty())
    {
        std::cerr << "fioRemove error: Path translate fail" << std::endl;
        setReturnS32(ctx, -1);
        return;
    }

#ifdef _WIN32
    int ret = -1;
#else
    int ret = ::unlink(hostPath.c_str());
#endif

    if (ret != 0)
    {
        std::cerr << "fioRemove error: unlink failed for '" << hostPath << "': " << strerror(errno) << std::endl;
        setReturnS32(ctx, -1);
    }
    else
    {
        setReturnS32(ctx, 0); // Success
    }
}
