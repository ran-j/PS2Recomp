void malloc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t size = getRegU32(ctx, 4); // $a0
    const uint32_t guestAddr = runtime ? runtime->guestMalloc(size) : 0u;
    setReturnU32(ctx, guestAddr);
}

void free(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t guestAddr = getRegU32(ctx, 4); // $a0
    if (runtime && guestAddr != 0u)
    {
        runtime->guestFree(guestAddr);
    }
}

void calloc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t count = getRegU32(ctx, 4); // $a0
    const uint32_t size = getRegU32(ctx, 5);  // $a1
    const uint32_t guestAddr = runtime ? runtime->guestCalloc(count, size) : 0u;
    setReturnU32(ctx, guestAddr);
}

void realloc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t oldGuestAddr = getRegU32(ctx, 4); // $a0
    const uint32_t newSize = getRegU32(ctx, 5);      // $a1
    const uint32_t newGuestAddr = runtime ? runtime->guestRealloc(oldGuestAddr, newSize) : 0u;
    setReturnU32(ctx, newGuestAddr);
}

void memcpy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t destAddr = getRegU32(ctx, 4); // $a0
    uint32_t srcAddr = getRegU32(ctx, 5);  // $a1
    size_t size = getRegU32(ctx, 6);       // $a2

    uint8_t *hostDest = getMemPtr(rdram, destAddr);
    const uint8_t *hostSrc = getConstMemPtr(rdram, srcAddr);

    if (hostDest && hostSrc)
    {
        ::memcpy(hostDest, hostSrc, size);
        ps2TraceGuestRangeWrite(rdram, destAddr, static_cast<uint32_t>(size), "memcpy", ctx);
    }
    else
    {
        std::cerr << "memcpy error: Attempted copy involving non-RDRAM address (or invalid RDRAM address)."
                  << " Dest: 0x" << std::hex << destAddr << " (host ptr valid: " << (hostDest != nullptr) << ")"
                  << ", Src: 0x" << srcAddr << " (host ptr valid: " << (hostSrc != nullptr) << ")" << std::dec
                  << ", Size: " << size << std::endl;
    }

    // returns dest pointer ($v0 = $a0)
    ctx->r[2] = ctx->r[4];
}

void memset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t destAddr = getRegU32(ctx, 4);       // $a0
    int value = (int)(getRegU32(ctx, 5) & 0xFF); // $a1 (char value)
    uint32_t size = getRegU32(ctx, 6);           // $a2

    uint8_t *hostDest = getMemPtr(rdram, destAddr);

    if (hostDest)
    {
        ::memset(hostDest, value, size);
        ps2TraceGuestRangeWrite(rdram, destAddr, size, "memset", ctx);
    }
    else
    {
        std::cerr << "memset error: Invalid address provided." << std::endl;
    }

    // returns dest pointer ($v0 = $a0)
    ctx->r[2] = ctx->r[4];
}

void memmove(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t destAddr = getRegU32(ctx, 4); // $a0
    uint32_t srcAddr = getRegU32(ctx, 5);  // $a1
    size_t size = getRegU32(ctx, 6);       // $a2

    uint8_t *hostDest = getMemPtr(rdram, destAddr);
    const uint8_t *hostSrc = getConstMemPtr(rdram, srcAddr);

    if (hostDest && hostSrc)
    {
        ::memmove(hostDest, hostSrc, size);
        ps2TraceGuestRangeWrite(rdram, destAddr, static_cast<uint32_t>(size), "memmove", ctx);
    }
    else
    {
        std::cerr << "memmove error: Attempted move involving potentially invalid RDRAM address."
                  << " Dest: 0x" << std::hex << destAddr << " (host ptr valid: " << (hostDest != nullptr) << ")"
                  << ", Src: 0x" << srcAddr << " (host ptr valid: " << (hostSrc != nullptr) << ")" << std::dec
                  << ", Size: " << size << std::endl;
    }

    // returns dest pointer ($v0 = $a0)
    ctx->r[2] = ctx->r[4];
}

void memcmp(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t ptr1Addr = getRegU32(ctx, 4); // $a0
    uint32_t ptr2Addr = getRegU32(ctx, 5); // $a1
    uint32_t size = getRegU32(ctx, 6);     // $a2

    const uint8_t *hostPtr1 = getConstMemPtr(rdram, ptr1Addr);
    const uint8_t *hostPtr2 = getConstMemPtr(rdram, ptr2Addr);
    int result = 0;

    if (hostPtr1 && hostPtr2)
    {
        result = ::memcmp(hostPtr1, hostPtr2, size);
    }
    else
    {
        std::cerr << "memcmp error: Invalid address provided."
                  << " Ptr1: 0x" << std::hex << ptr1Addr << " (host ptr valid: " << (hostPtr1 != nullptr) << ")"
                  << ", Ptr2: 0x" << ptr2Addr << " (host ptr valid: " << (hostPtr2 != nullptr) << ")" << std::dec
                  << std::endl;

        result = (hostPtr1 == nullptr) - (hostPtr2 == nullptr);
        if (result == 0)
            result = 1; // If both null, still different? Or 0?
    }
    setReturnS32(ctx, result);
}

void strcpy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t destAddr = getRegU32(ctx, 4); // $a0
    uint32_t srcAddr = getRegU32(ctx, 5);  // $a1

    char *hostDest = reinterpret_cast<char *>(getMemPtr(rdram, destAddr));
    const char *hostSrc = reinterpret_cast<const char *>(getConstMemPtr(rdram, srcAddr));

    if (hostDest && hostSrc)
    {
        ::strcpy(hostDest, hostSrc);
        ps2TraceGuestRangeWrite(rdram, destAddr, static_cast<uint32_t>(::strlen(hostSrc) + 1u), "strcpy", ctx);
    }
    else
    {
        std::cerr << "strcpy error: Invalid address provided."
                  << " Dest: 0x" << std::hex << destAddr << " (host ptr valid: " << (hostDest != nullptr) << ")"
                  << ", Src: 0x" << srcAddr << " (host ptr valid: " << (hostSrc != nullptr) << ")" << std::dec
                  << std::endl;
    }

    // returns dest pointer ($v0 = $a0)
    ctx->r[2] = ctx->r[4];
}

void strncpy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t destAddr = getRegU32(ctx, 4); // $a0
    uint32_t srcAddr = getRegU32(ctx, 5);  // $a1
    uint32_t size = getRegU32(ctx, 6);     // $a2

    char *hostDest = reinterpret_cast<char *>(getMemPtr(rdram, destAddr));
    const char *hostSrc = reinterpret_cast<const char *>(getConstMemPtr(rdram, srcAddr));

    if (hostDest && hostSrc)
    {
        ::strncpy(hostDest, hostSrc, size);
        ps2TraceGuestRangeWrite(rdram, destAddr, size, "strncpy", ctx);
    }
    else
    {
        std::cerr << "strncpy error: Invalid address provided."
                  << " Dest: 0x" << std::hex << destAddr << " (host ptr valid: " << (hostDest != nullptr) << ")"
                  << ", Src: 0x" << srcAddr << " (host ptr valid: " << (hostSrc != nullptr) << ")" << std::dec
                  << std::endl;
    }
    // returns dest pointer ($v0 = $a0)
    ctx->r[2] = ctx->r[4];
}

void strlen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t strAddr = getRegU32(ctx, 4); // $a0
    const char *hostStr = reinterpret_cast<const char *>(getConstMemPtr(rdram, strAddr));
    size_t len = 0;

    if (hostStr)
    {
        len = ::strlen(hostStr);
    }
    else
    {
        std::cerr << "strlen error: Invalid address provided: 0x" << std::hex << strAddr << std::dec << std::endl;
    }
    setReturnU32(ctx, (uint32_t)len);
}

void strcmp(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t str1Addr = getRegU32(ctx, 4); // $a0
    uint32_t str2Addr = getRegU32(ctx, 5); // $a1

    const char *hostStr1 = reinterpret_cast<const char *>(getConstMemPtr(rdram, str1Addr));
    const char *hostStr2 = reinterpret_cast<const char *>(getConstMemPtr(rdram, str2Addr));
    int result = 0;

    if (hostStr1 && hostStr2)
    {
        result = ::strcmp(hostStr1, hostStr2);
    }
    else
    {
        std::cerr << "strcmp error: Invalid address provided."
                  << " Str1: 0x" << std::hex << str1Addr << " (host ptr valid: " << (hostStr1 != nullptr) << ")"
                  << ", Str2: 0x" << str2Addr << " (host ptr valid: " << (hostStr2 != nullptr) << ")" << std::dec
                  << std::endl;
        // Return non-zero on error, consistent with memcmp error handling
        result = (hostStr1 == nullptr) - (hostStr2 == nullptr);
        if (result == 0 && hostStr1 == nullptr)
            result = 1; // Both null -> treat as different? Or 0? Let's say different.
    }
    setReturnS32(ctx, result);
}

void strncmp(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t str1Addr = getRegU32(ctx, 4); // $a0
    uint32_t str2Addr = getRegU32(ctx, 5); // $a1
    uint32_t size = getRegU32(ctx, 6);     // $a2

    const char *hostStr1 = reinterpret_cast<const char *>(getConstMemPtr(rdram, str1Addr));
    const char *hostStr2 = reinterpret_cast<const char *>(getConstMemPtr(rdram, str2Addr));
    int result = 0;

    if (hostStr1 && hostStr2)
    {
        result = ::strncmp(hostStr1, hostStr2, size);
    }
    else
    {
        std::cerr << "strncmp error: Invalid address provided."
                  << " Str1: 0x" << std::hex << str1Addr << " (host ptr valid: " << (hostStr1 != nullptr) << ")"
                  << ", Str2: 0x" << str2Addr << " (host ptr valid: " << (hostStr2 != nullptr) << ")" << std::dec
                  << std::endl;
        result = (hostStr1 == nullptr) - (hostStr2 == nullptr);
        if (result == 0 && hostStr1 == nullptr)
            result = 1; // Both null -> different
    }
    setReturnS32(ctx, result);
}

void strcat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t destAddr = getRegU32(ctx, 4); // $a0
    uint32_t srcAddr = getRegU32(ctx, 5);  // $a1

    char *hostDest = reinterpret_cast<char *>(getMemPtr(rdram, destAddr));
    const char *hostSrc = reinterpret_cast<const char *>(getConstMemPtr(rdram, srcAddr));

    if (hostDest && hostSrc)
    {
        ::strcat(hostDest, hostSrc);
    }
    else
    {
        std::cerr << "strcat error: Invalid address provided."
                  << " Dest: 0x" << std::hex << destAddr << " (host ptr valid: " << (hostDest != nullptr) << ")"
                  << ", Src: 0x" << srcAddr << " (host ptr valid: " << (hostSrc != nullptr) << ")" << std::dec
                  << std::endl;
    }

    // returns dest pointer ($v0 = $a0)
    ctx->r[2] = ctx->r[4];
}

void strncat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t destAddr = getRegU32(ctx, 4); // $a0
    uint32_t srcAddr = getRegU32(ctx, 5);  // $a1
    uint32_t size = getRegU32(ctx, 6);     // $a2

    char *hostDest = reinterpret_cast<char *>(getMemPtr(rdram, destAddr));
    const char *hostSrc = reinterpret_cast<const char *>(getConstMemPtr(rdram, srcAddr));

    if (hostDest && hostSrc)
    {
        ::strncat(hostDest, hostSrc, size);
    }
    else
    {
        std::cerr << "strncat error: Invalid address provided."
                  << " Dest: 0x" << std::hex << destAddr << " (host ptr valid: " << (hostDest != nullptr) << ")"
                  << ", Src: 0x" << srcAddr << " (host ptr valid: " << (hostSrc != nullptr) << ")" << std::dec
                  << std::endl;
    }

    // returns dest pointer ($v0 = $a0)
    ctx->r[2] = ctx->r[4];
}

void strchr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t strAddr = getRegU32(ctx, 4);            // $a0
    int char_code = (int)(getRegU32(ctx, 5) & 0xFF); // $a1 (char value)

    const char *hostStr = reinterpret_cast<const char *>(getConstMemPtr(rdram, strAddr));
    char *foundPtr = nullptr;
    uint32_t resultAddr = 0;

    if (hostStr)
    {
        foundPtr = ::strchr(const_cast<char *>(hostStr), char_code);
        if (foundPtr)
        {
            resultAddr = hostPtrToPs2Addr(rdram, foundPtr);
        }
    }
    else
    {
        std::cerr << "strchr error: Invalid address provided: 0x" << std::hex << strAddr << std::dec << std::endl;
    }

    // returns PS2 address or 0 (NULL)
    setReturnU32(ctx, resultAddr);
}

void strrchr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t strAddr = getRegU32(ctx, 4);            // $a0
    int char_code = (int)(getRegU32(ctx, 5) & 0xFF); // $a1 (char value)

    const char *hostStr = reinterpret_cast<const char *>(getConstMemPtr(rdram, strAddr));
    char *foundPtr = nullptr;
    uint32_t resultAddr = 0;

    if (hostStr)
    {
        foundPtr = ::strrchr(const_cast<char *>(hostStr), char_code); // Use const_cast carefully
        if (foundPtr)
        {
            resultAddr = hostPtrToPs2Addr(rdram, foundPtr);
        }
    }
    else
    {
        std::cerr << "strrchr error: Invalid address provided: 0x" << std::hex << strAddr << std::dec << std::endl;
    }

    // returns PS2 address or 0 (NULL)
    setReturnU32(ctx, resultAddr);
}

void strstr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t haystackAddr = getRegU32(ctx, 4); // $a0
    uint32_t needleAddr = getRegU32(ctx, 5);   // $a1

    const char *hostHaystack = reinterpret_cast<const char *>(getConstMemPtr(rdram, haystackAddr));
    const char *hostNeedle = reinterpret_cast<const char *>(getConstMemPtr(rdram, needleAddr));
    char *foundPtr = nullptr;
    uint32_t resultAddr = 0;

    if (hostHaystack && hostNeedle)
    {
        foundPtr = ::strstr(const_cast<char *>(hostHaystack), hostNeedle);
        if (foundPtr)
        {
            resultAddr = hostPtrToPs2Addr(rdram, foundPtr);
        }
    }
    else
    {
        std::cerr << "strstr error: Invalid address provided."
                  << " Haystack: 0x" << std::hex << haystackAddr << " (host ptr valid: " << (hostHaystack != nullptr) << ")"
                  << ", Needle: 0x" << needleAddr << " (host ptr valid: " << (hostNeedle != nullptr) << ")" << std::dec
                  << std::endl;
    }

    // returns PS2 address or 0 (NULL)
    setReturnU32(ctx, resultAddr);
}

void printf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t format_addr = getRegU32(ctx, 4); // $a0
    const std::string formatOwned = readPs2CStringBounded(rdram, runtime, format_addr, 1024);
    int ret = -1;

    if (format_addr != 0)
    {
        std::string rendered = formatPs2StringWithArgs(rdram, ctx, runtime, formatOwned.c_str(), 1);
        if (rendered.size() > 2048)
        {
            rendered.resize(2048);
        }
        const std::string logLine = sanitizeForLog(rendered);
        uint32_t count = 0;
        {
            std::lock_guard<std::mutex> lock(g_printfLogMutex);
            count = ++g_printfLogCount;
        }
        if (count <= kMaxPrintfLogs)
        {
            std::cout << "PS2 printf: " << logLine;
            std::cout << std::flush;
        }
        else if (count == kMaxPrintfLogs + 1)
        {
            std::cerr << "PS2 printf logging suppressed after " << kMaxPrintfLogs << " lines" << std::endl;
        }
        ret = static_cast<int>(rendered.size());
    }
    else
    {
        std::cerr << "printf error: Invalid format string address provided: 0x" << std::hex << format_addr << std::dec << std::endl;
    }

    // returns the number of characters written, or negative on error.
    setReturnS32(ctx, ret);
}

void sprintf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t str_addr = getRegU32(ctx, 4);    // $a0
    uint32_t format_addr = getRegU32(ctx, 5); // $a1

    const std::string formatOwned = readPs2CStringBounded(rdram, runtime, format_addr, 1024);
    int ret = -1;

    if (format_addr != 0)
    {
        const uint32_t watchBase = ps2PathWatchPhysAddr();
        const uint32_t watchEnd = watchBase + PS2_PATH_WATCH_BYTES;
        const uint32_t dest = str_addr & PS2_RAM_MASK;
        const bool touchesWatch = dest < watchEnd && dest >= watchBase;
        static uint32_t watchSprintfLogCount = 0;
        if (touchesWatch && watchSprintfLogCount < 64u)
        {
            const uint32_t arg0 = getRegU32(ctx, 6);
            const uint32_t arg1 = getRegU32(ctx, 7);
            std::cout << "[watch:sprintf] dest=0x" << std::hex << str_addr
                      << " fmt@0x" << format_addr
                      << " arg0=0x" << arg0
                      << " arg1=0x" << arg1
                      << " fmt=\"" << sanitizeForLog(readPs2CStringBounded(rdram, runtime, format_addr, 64)) << "\""
                      << " s0=\"" << sanitizeForLog(readPs2CStringBounded(rdram, runtime, arg0, 64)) << "\""
                      << " s1=\"" << sanitizeForLog(readPs2CStringBounded(rdram, runtime, arg1, 64)) << "\""
                      << std::dec << std::endl;
            ++watchSprintfLogCount;
        }

        std::string rendered = formatPs2StringWithArgs(rdram, ctx, runtime, formatOwned.c_str(), 2);
        if (rendered.size() >= kMaxFormattedOutputBytes)
        {
            rendered.resize(kMaxFormattedOutputBytes - 1);
        }
        const size_t writeLen = rendered.size() + 1u;
        if (writeGuestBytes(rdram, runtime, str_addr, reinterpret_cast<const uint8_t *>(rendered.c_str()), writeLen))
        {
            ps2TraceGuestRangeWrite(rdram, str_addr, static_cast<uint32_t>(writeLen), "sprintf", ctx);
            ret = static_cast<int>(rendered.size());
        }
        else
        {
            std::cerr << "sprintf error: Failed to write destination buffer at 0x"
                      << std::hex << str_addr << std::dec << std::endl;
        }
    }
    else
    {
        std::cerr << "sprintf error: Invalid format address provided."
                  << " Dest: 0x" << std::hex << str_addr
                  << ", Format: 0x" << format_addr << std::dec
                  << std::endl;
    }

    // returns the number of characters written (excluding null), or negative on error.
    setReturnS32(ctx, ret);
}

void snprintf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t str_addr = getRegU32(ctx, 4);    // $a0
    size_t size = getRegU32(ctx, 5);          // $a1
    uint32_t format_addr = getRegU32(ctx, 6); // $a2
    const std::string formatOwned = readPs2CStringBounded(rdram, runtime, format_addr, 1024);
    int ret = -1;

    if (format_addr != 0)
    {
        std::string rendered = formatPs2StringWithArgs(rdram, ctx, runtime, formatOwned.c_str(), 3);
        ret = static_cast<int>(rendered.size());

        if (size > 0)
        {
            const size_t copyLen = std::min<size_t>(size - 1, rendered.size());
            std::vector<uint8_t> output(copyLen + 1u, 0u);
            if (copyLen > 0u)
            {
                std::memcpy(output.data(), rendered.data(), copyLen);
            }
            if (writeGuestBytes(rdram, runtime, str_addr, output.data(), output.size()))
            {
                ps2TraceGuestRangeWrite(rdram, str_addr, static_cast<uint32_t>(output.size()), "snprintf", ctx);
            }
            else
            {
                std::cerr << "snprintf error: Failed to write destination buffer at 0x"
                          << std::hex << str_addr << std::dec << std::endl;
                ret = -1;
            }
        }
    }
    else
    {
        std::cerr << "snprintf error: Invalid address provided or size is zero."
                  << " Dest: 0x" << std::hex << str_addr
                  << ", Format: 0x" << format_addr << std::dec
                  << ", Size: " << size << std::endl;
    }

    // returns the number of characters that *would* have been written
    // if size was large enough (excluding null), or negative on error.
    setReturnS32(ctx, ret);
}

void puts(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t strAddr = getRegU32(ctx, 4); // $a0
    const char *hostStr = reinterpret_cast<const char *>(getConstMemPtr(rdram, strAddr));
    int result = EOF;

    if (hostStr)
    {
        result = std::puts(hostStr); // std::puts adds a newline
        std::fflush(stdout);         // Ensure output appears
    }
    else
    {
        std::cerr << "puts error: Invalid address provided: 0x" << std::hex << strAddr << std::dec << std::endl;
    }

    // returns non-negative on success, EOF on error.
    setReturnS32(ctx, result >= 0 ? 0 : -1); // PS2 might expect 0/-1 rather than EOF
}

void fopen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t pathAddr = getRegU32(ctx, 4); // $a0
    uint32_t modeAddr = getRegU32(ctx, 5); // $a1

    const char *hostPath = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
    const char *hostMode = reinterpret_cast<const char *>(getConstMemPtr(rdram, modeAddr));
    uint32_t file_handle = 0;

    if (hostPath && hostMode)
    {
        // TODO: Add translation for PS2 paths like mc0:, host:, cdrom:, etc.
        // treating as direct host path
        std::cout << "ps2_stub fopen: path='" << hostPath << "', mode='" << hostMode << "'" << std::endl;
        FILE *fp = ::fopen(hostPath, hostMode);
        if (fp)
        {
            std::lock_guard<std::mutex> lock(g_file_mutex);
            file_handle = generate_file_handle();
            g_file_map[file_handle] = fp;
            std::cout << "  -> handle=0x" << std::hex << file_handle << std::dec << std::endl;
        }
        else
        {
            std::cerr << "ps2_stub fopen error: Failed to open '" << hostPath << "' with mode '" << hostMode << "'. Error: " << strerror(errno) << std::endl;
        }
    }
    else
    {
        std::cerr << "fopen error: Invalid address provided for path or mode."
                  << " Path: 0x" << std::hex << pathAddr << " (host ptr valid: " << (hostPath != nullptr) << ")"
                  << ", Mode: 0x" << modeAddr << " (host ptr valid: " << (hostMode != nullptr) << ")" << std::dec
                  << std::endl;
    }
    // returns a file handle (non-zero) on success, or NULL (0) on error.
    setReturnU32(ctx, file_handle);
}

void fclose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t file_handle = getRegU32(ctx, 4); // $a0
    int ret = EOF;                            // Default to error

    if (file_handle != 0)
    {
        std::lock_guard<std::mutex> lock(g_file_mutex);
        auto it = g_file_map.find(file_handle);
        if (it != g_file_map.end())
        {
            FILE *fp = it->second;
            ret = ::fclose(fp);
            g_file_map.erase(it);
        }
        else
        {
            std::cerr << "ps2_stub fclose error: Invalid file handle 0x" << std::hex << file_handle << std::dec << std::endl;
        }
    }
    else
    {
        // Closing NULL handle in Standard C defines this as no-op
        ret = 0;
    }

    // returns 0 on success, EOF on error.
    setReturnS32(ctx, ret);
}

void fread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t ptrAddr = getRegU32(ctx, 4);     // $a0 (buffer)
    uint32_t size = getRegU32(ctx, 5);        // $a1 (element size)
    uint32_t count = getRegU32(ctx, 6);       // $a2 (number of elements)
    uint32_t file_handle = getRegU32(ctx, 7); // $a3 (file handle)
    size_t items_read = 0;

    uint8_t *hostPtr = getMemPtr(rdram, ptrAddr);
    FILE *fp = get_file_ptr(file_handle);

    if (hostPtr && fp && size > 0 && count > 0)
    {
        items_read = ::fread(hostPtr, size, count, fp);
    }
    else
    {
        std::cerr << "fread error: Invalid arguments."
                  << " Ptr: 0x" << std::hex << ptrAddr << " (host ptr valid: " << (hostPtr != nullptr) << ")"
                  << ", Handle: 0x" << file_handle << " (file valid: " << (fp != nullptr) << ")" << std::dec
                  << ", Size: " << size << ", Count: " << count << std::endl;
    }
    // returns the number of items successfully read.
    setReturnU32(ctx, (uint32_t)items_read);
}

void fwrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t ptrAddr = getRegU32(ctx, 4);     // $a0 (buffer)
    uint32_t size = getRegU32(ctx, 5);        // $a1 (element size)
    uint32_t count = getRegU32(ctx, 6);       // $a2 (number of elements)
    uint32_t file_handle = getRegU32(ctx, 7); // $a3 (file handle)
    size_t items_written = 0;

    const uint8_t *hostPtr = getConstMemPtr(rdram, ptrAddr);
    FILE *fp = get_file_ptr(file_handle);

    if (hostPtr && fp && size > 0 && count > 0)
    {
        items_written = ::fwrite(hostPtr, size, count, fp);
    }
    else
    {
        std::cerr << "fwrite error: Invalid arguments."
                  << " Ptr: 0x" << std::hex << ptrAddr << " (host ptr valid: " << (hostPtr != nullptr) << ")"
                  << ", Handle: 0x" << file_handle << " (file valid: " << (fp != nullptr) << ")" << std::dec
                  << ", Size: " << size << ", Count: " << count << std::endl;
    }
    // returns the number of items successfully written.
    setReturnU32(ctx, (uint32_t)items_written);
}

void fprintf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t file_handle = getRegU32(ctx, 4); // $a0
    uint32_t format_addr = getRegU32(ctx, 5); // $a1
    FILE *fp = get_file_ptr(file_handle);
    const std::string formatOwned = readPs2CStringBounded(rdram, runtime, format_addr, 1024);
    int ret = -1;

    if (fp && format_addr != 0)
    {
        std::string rendered = formatPs2StringWithArgs(rdram, ctx, runtime, formatOwned.c_str(), 2);
        ret = std::fprintf(fp, "%s", rendered.c_str());
    }
    else
    {
        std::cerr << "fprintf error: Invalid file handle or format address."
                  << " Handle: 0x" << std::hex << file_handle << " (file valid: " << (fp != nullptr) << ")"
                  << ", Format: 0x" << format_addr << std::dec
                  << std::endl;
    }

    // returns the number of characters written, or negative on error.
    setReturnS32(ctx, ret);
}

void fseek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t file_handle = getRegU32(ctx, 4); // $a0
    long offset = (long)getRegU32(ctx, 5);    // $a1 (Note: might need 64-bit for large files?)
    int whence = (int)getRegU32(ctx, 6);      // $a2 (SEEK_SET, SEEK_CUR, SEEK_END)
    int ret = -1;                             // Default error

    FILE *fp = get_file_ptr(file_handle);

    if (fp)
    {
        // Ensure whence is valid (0, 1, 2)
        if (whence >= 0 && whence <= 2)
        {
            ret = ::fseek(fp, offset, whence);
        }
        else
        {
            std::cerr << "fseek error: Invalid whence value: " << whence << std::endl;
        }
    }
    else
    {
        std::cerr << "fseek error: Invalid file handle 0x" << std::hex << file_handle << std::dec << std::endl;
    }

    // returns 0 on success, non-zero on error.
    setReturnS32(ctx, ret);
}

void ftell(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t file_handle = getRegU32(ctx, 4); // $a0
    long ret = -1L;

    FILE *fp = get_file_ptr(file_handle);

    if (fp)
    {
        ret = ::ftell(fp);
    }
    else
    {
        std::cerr << "ftell error: Invalid file handle 0x" << std::hex << file_handle << std::dec << std::endl;
    }

    // returns the current position, or -1L on error.
    if (ret > 0xFFFFFFFFL || ret < 0)
    {
        setReturnS32(ctx, -1);
    }
    else
    {
        setReturnU32(ctx, (uint32_t)ret);
    }
}

void fflush(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t file_handle = getRegU32(ctx, 4); // $a0
    int ret = EOF;                            // Default error

    // If handle is 0 fflush flushes *all* output streams.
    if (file_handle == 0)
    {
        ret = ::fflush(NULL);
    }
    else
    {
        FILE *fp = get_file_ptr(file_handle);
        if (fp)
        {
            ret = ::fflush(fp);
        }
        else
        {
            std::cerr << "fflush error: Invalid file handle 0x" << std::hex << file_handle << std::dec << std::endl;
        }
    }
    // returns 0 on success, EOF on error.
    setReturnS32(ctx, ret);
}

void sqrt(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    float arg = ctx->f[12];
    ctx->f[0] = ::sqrtf(arg);
}

void sin(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    float arg = ctx->f[12];
    ctx->f[0] = ::sinf(arg);
}

void __kernel_sinf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const float x = ctx->f[12];
    const float y = ctx->f[13];
    const int32_t iy = static_cast<int32_t>(getRegU32(ctx, 4));
    ctx->f[0] = ::sinf(x + (iy != 0 ? y : 0.0f));
}

void cos(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    float arg = ctx->f[12];
    ctx->f[0] = ::cosf(arg);
}

void __kernel_cosf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const float x = ctx->f[12];
    const float y = ctx->f[13];
    ctx->f[0] = ::cosf(x + y);
}

void __ieee754_rem_pio2f(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const float x = ctx->f[12];
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kHalfPi = kPi * 0.5f;
    constexpr float kInvHalfPi = 2.0f / kPi;
    const int32_t n = static_cast<int32_t>(std::nearbyintf(x * kInvHalfPi));
    const float y0 = x - (static_cast<float>(n) * kHalfPi);
    const float y1 = 0.0f;

    const uint32_t yOutAddr = getRegU32(ctx, 4);
    if (float *yOut0 = reinterpret_cast<float *>(getMemPtr(rdram, yOutAddr)); yOut0)
    {
        *yOut0 = y0;
    }
    if (float *yOut1 = reinterpret_cast<float *>(getMemPtr(rdram, yOutAddr + 4)); yOut1)
    {
        *yOut1 = y1;
    }

    setReturnS32(ctx, n);
}

void tan(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    float arg = ctx->f[12];
    ctx->f[0] = ::tanf(arg);
}

void atan2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    float y = ctx->f[12];
    float x = ctx->f[14];
    ctx->f[0] = ::atan2f(y, x);
}

void pow(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    float base = ctx->f[12];
    float exp = ctx->f[14];
    ctx->f[0] = ::powf(base, exp);
}

void exp(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    float arg = ctx->f[12];
    ctx->f[0] = ::expf(arg);
}

void log(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    float arg = ctx->f[12];
    ctx->f[0] = ::logf(arg);
}

void log10(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    float arg = ctx->f[12];
    ctx->f[0] = ::log10f(arg);
}

void ceil(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    float arg = ctx->f[12];
    ctx->f[0] = ::ceilf(arg);
}

void floor(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    float arg = ctx->f[12];
    ctx->f[0] = ::floorf(arg);
}

void fabs(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    float arg = ctx->f[12];
    ctx->f[0] = ::fabsf(arg);
}
