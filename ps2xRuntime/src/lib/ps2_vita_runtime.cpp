#if defined(PLATFORM_VITA)

#include <cstdarg>
#include <cstdio>

// Heap sizes must live in a native object as well(bc of LTO).
// the newlib malloc/new arena (PS2 guest RAM 32MB + GS + buffers live here).
extern "C" unsigned int _newlib_heap_size_user = 96u * 1024u * 1024u;
extern "C" unsigned int sceLibcHeapSize = 8u * 1024u * 1024u;

namespace
{
    void ttyPuts(const char *text)
    {
        if (text)
        {
            std::fputs(text, stdout); // Vita TTY (capturable via PrincessLog etc.)
        }
    }
}

// Overrides raylib's debugnet transport with TTY output.
extern "C"
{
    int debugNetInit(const char *, int, int)
    {
        return 0;
    }

    void debugNetFinish(void)
    {
    }

    int debugNetUDPSend(const char *text)
    {
        ttyPuts(text);
        return 0;
    }

    int debugNetUDPPrintf(const char *fmt, ...)
    {
        char line[512];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(line, sizeof(line), fmt, args);
        va_end(args);
        ttyPuts(line);
        return 0;
    }

    int debugNetPrintf(int level, const char *fmt, ...)
    {
        (void)level;
        char line[512];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(line, sizeof(line), fmt, args);
        va_end(args);
        ttyPuts(line);
        return 0;
    }
}

#endif // PLATFORM_VITA
