#include "ps2_stubs.h"
#include "ps2_runtime.h"
#include <iostream>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <mutex>

// --- Memory management ---
namespace
{
    constexpr size_t STUB_HEAP_SIZE = 8 * 1024 * 1024; // 8 MB simple heap
    std::vector<uint8_t> g_stub_heap(STUB_HEAP_SIZE);
    size_t g_stub_heap_ptr = 0;

    std::unordered_map<uint32_t, size_t> g_allocations;
    constexpr uint32_t STUB_HEAP_BASE_ADDR = 0x0F000000; // Unused area in PS2 RAM
}

// --- File I/O ---
namespace
{
    std::unordered_map<uint32_t, FILE *> g_file_map;
    uint32_t g_next_file_handle = 1; // Start file handles > 0 (0 is NULL)
    std::mutex g_file_mutex;

    uint32_t generate_file_handle()
    {
        uint32_t handle = 0;
        do
        {
            handle = g_next_file_handle++;
            if (g_next_file_handle == 0)
                g_next_file_handle = 1;
        } while (handle == 0 || g_file_map.count(handle));
        return handle;
    }

    FILE *get_file_ptr(uint32_t handle)
    {
        if (handle == 0)
            return nullptr;
        std::lock_guard<std::mutex> lock(g_file_mutex);
        auto it = g_file_map.find(handle);
        return (it != g_file_map.end()) ? it->second : nullptr;
    }

}

namespace
{
    // convert a host pointer within rdram back to a PS2 address
    uint32_t hostPtrToPs2Addr(uint8_t *rdram, const void *hostPtr)
    {
        if (!hostPtr)
            return 0; // Handle NULL pointer case

        const uint8_t *ptr_u8 = static_cast<const uint8_t *>(hostPtr);
        std::ptrdiff_t offset = ptr_u8 - rdram;

        // Check if is in rdram range
        if (offset >= 0 && static_cast<size_t>(offset) < PS2_RAM_SIZE)
        {
            return PS2_RAM_BASE + static_cast<uint32_t>(offset);
        }
        else
        {
            std::cerr << "Warning: hostPtrToPs2Addr failed - host pointer " << hostPtr << " is outside rdram range [" << static_cast<void *>(rdram) << ", " << static_cast<void *>(rdram + PS2_RAM_SIZE) << ")" << std::endl;
            return 0;
        }
    }
}

namespace
{
    std::unordered_map<uint32_t, void *> g_alloc_map; // Map handle -> host ptr
    std::unordered_map<void *, size_t> g_size_map;    // Map host ptr -> size
    uint32_t g_next_handle = 0x7F000000;              // Start handles in a high, unlikely range
    std::mutex g_alloc_mutex;                         // Mutex for thread safety

    uint32_t generate_handle()
    {
        // Very basic handle generation. We could wrap around or collide eventually.
        uint32_t handle = 0;
        do
        {
            handle = g_next_handle++;
            if (g_next_handle == 0) // Skip 0 if it wraps around
                g_next_handle = 1;
        } while (handle == 0 || g_alloc_map.count(handle));
        return handle;
    }
}

namespace ps2_stubs
{

    void ps2_stubs::malloc(uint8_t *rdram, R5900Context *ctx)
    {
        size_t size = getRegU32(ctx, 4); // $a0
        uint32_t handle = 0;

        if (size > 0)
        {
            void *ptr = ::malloc(size);
            if (ptr)
            {
                std::lock_guard<std::mutex> lock(g_alloc_mutex);
                handle = generate_handle();
                g_alloc_map[handle] = ptr;
                g_size_map[ptr] = size;
                std::cout << "ps2_stub malloc: size=" << size << " -> handle=0x" << std::hex << handle << std::dec << std::endl;
            }
            else
            {
                std::cerr << "ps2_stub malloc error: Host allocation failed for size " << size << std::endl;
            }
        }
        // returns handle (0 if size=0 or allocation failed)
        setReturnU32(ctx, handle); 
    }

    void ps2_stubs::free(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t handle = getRegU32(ctx, 4); // $a0

        std::cout << "ps2_stub free: handle=0x" << std::hex << handle << std::dec << std::endl;

        if (handle != 0)
        {
            std::lock_guard<std::mutex> lock(g_alloc_mutex);
            auto it = g_alloc_map.find(handle);
            if (it != g_alloc_map.end())
            {
                void *ptr = it->second;
                ::free(ptr);
                g_size_map.erase(ptr);
                g_alloc_map.erase(it);
            }
            else
            {
                // Commented out because some programs might free static/non-heap memory
                // std::cerr << "ps2_stub free error: Invalid handle 0x" << std::hex << handle << std::dec << std::endl;
            }
        }
        // free dont have return
    }

    void ps2_stubs::calloc(uint8_t *rdram, R5900Context *ctx)
    {
        size_t num = getRegU32(ctx, 4);  // $a0
        size_t size = getRegU32(ctx, 5); // $a1
        uint32_t handle = 0;
        size_t total_size = num * size;

        if (total_size > 0 && (size == 0 || total_size / size == num)) // maybe we can ignore this overflow check
        {
            void *ptr = ::calloc(num, size);
            if (ptr)
            {
                std::lock_guard<std::mutex> lock(g_alloc_mutex);
                handle = generate_handle();
                g_alloc_map[handle] = ptr;
                g_size_map[ptr] = total_size;
                std::cout << "ps2_stub calloc: num=" << num << ", size=" << size << " -> handle=0x" << std::hex << handle << std::dec << std::endl;
            }
            else
            {
                std::cerr << "ps2_stub calloc error: Host allocation failed for " << num << " * " << size << " bytes" << std::endl;
            }
        }
        // retuns handle (0 if size=0 or allocation failed)
        setReturnU32(ctx, handle);
    }

    void ps2_stubs::realloc(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t old_handle = getRegU32(ctx, 4); // $a0
        size_t new_size = getRegU32(ctx, 5);     // $a1
        uint32_t new_handle = 0;
        void *old_ptr = nullptr;

        std::cout << "ps2_stub realloc: old_handle=0x" << std::hex << old_handle << ", new_size=" << std::dec << new_size << std::endl;

        if (old_handle == 0)
        {
            void *new_ptr_alloc = ::malloc(new_size);
            if (new_ptr_alloc)
            {
                std::lock_guard<std::mutex> lock(g_alloc_mutex);
                new_handle = generate_handle();
                g_alloc_map[new_handle] = new_ptr_alloc;
                g_size_map[new_ptr_alloc] = new_size;
            }
            else if (new_size > 0)
            {
                std::cerr << "ps2_stub realloc (as malloc) error: Host allocation failed for size " << new_size << std::endl;
            }
        }
        else if (new_size == 0)
        {
            std::lock_guard<std::mutex> lock(g_alloc_mutex);
            auto it = g_alloc_map.find(old_handle);
            if (it != g_alloc_map.end())
            {
                old_ptr = it->second;
                ::free(old_ptr);
                g_size_map.erase(old_ptr);
                g_alloc_map.erase(it);
            }
            else
            {
                std::cerr << "ps2_stub realloc (as free) error: Invalid handle 0x" << std::hex << old_handle << std::dec << std::endl;
            }
            new_handle = 0;
        }
        else
        {
            std::lock_guard<std::mutex> lock(g_alloc_mutex);
            auto it = g_alloc_map.find(old_handle);
            if (it != g_alloc_map.end())
            {
                old_ptr = it->second;
                void *new_ptr = ::realloc(old_ptr, new_size);
                if (new_ptr)
                {
                    if (new_ptr != old_ptr)
                    {
                        g_size_map.erase(old_ptr);
                        g_alloc_map.erase(it);

                        new_handle = generate_handle();
                        g_alloc_map[new_handle] = new_ptr;
                        g_size_map[new_ptr] = new_size;
                    }
                    else
                    {
                        g_size_map[new_ptr] = new_size;
                        new_handle = old_handle;
                    }
                }
                else
                {
                    std::cerr << "ps2_stub realloc error: Host reallocation failed for handle 0x" << std::hex << old_handle << " to size " << std::dec << new_size << std::endl;
                    new_handle = 0;
                }
            }
            else
            {
                std::cerr << "ps2_stub realloc error: Invalid handle 0x" << std::hex << old_handle << std::dec << std::endl;
                new_handle = 0;
            }
        }

        setReturnU32(ctx, new_handle);
    }

    void ps2_stubs::memcpy(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t destAddr = getRegU32(ctx, 4); // $a0
        uint32_t srcAddr = getRegU32(ctx, 5);  // $a1
        size_t size = getRegU32(ctx, 6);       // $a2

        uint8_t *hostDest = getMemPtr(rdram, destAddr);
        const uint8_t *hostSrc = getConstMemPtr(rdram, srcAddr);

        if (hostDest && hostSrc)
        {
            ::memcpy(hostDest, hostSrc, size);
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

    void ps2_stubs::memset(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t destAddr = getRegU32(ctx, 4);       // $a0
        int value = (int)(getRegU32(ctx, 5) & 0xFF); // $a1 (char value)
        uint32_t size = getRegU32(ctx, 6);           // $a2

        uint8_t *hostDest = getMemPtr(rdram, destAddr);

        if (hostDest)
        {
            ::memset(hostDest, value, size);
        }
        else
        {
            std::cerr << "memset error: Invalid address provided." << std::endl;
        }

        // returns dest pointer ($v0 = $a0)
        ctx->r[2] = ctx->r[4];
    }

    void ps2_stubs::memmove(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t destAddr = getRegU32(ctx, 4); // $a0
        uint32_t srcAddr = getRegU32(ctx, 5);  // $a1
        size_t size = getRegU32(ctx, 6);       // $a2

        uint8_t *hostDest = getMemPtr(rdram, destAddr);
        const uint8_t *hostSrc = getConstMemPtr(rdram, srcAddr);

        if (hostDest && hostSrc)
        {
            ::memmove(hostDest, hostSrc, size);
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

    void ps2_stubs::memcmp(uint8_t *rdram, R5900Context *ctx)
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

    void ps2_stubs::strcpy(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t destAddr = getRegU32(ctx, 4); // $a0
        uint32_t srcAddr = getRegU32(ctx, 5);  // $a1

        char *hostDest = reinterpret_cast<char *>(getMemPtr(rdram, destAddr));
        const char *hostSrc = reinterpret_cast<const char *>(getConstMemPtr(rdram, srcAddr));

        if (hostDest && hostSrc)
        {
            ::strcpy(hostDest, hostSrc);
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

    void ps2_stubs::strncpy(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t destAddr = getRegU32(ctx, 4); // $a0
        uint32_t srcAddr = getRegU32(ctx, 5);  // $a1
        uint32_t size = getRegU32(ctx, 6);     // $a2

        char *hostDest = reinterpret_cast<char *>(getMemPtr(rdram, destAddr));
        const char *hostSrc = reinterpret_cast<const char *>(getConstMemPtr(rdram, srcAddr));

        if (hostDest && hostSrc)
        {
            ::strncpy(hostDest, hostSrc, size);
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

    void ps2_stubs::strlen(uint8_t *rdram, R5900Context *ctx)
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

    void ps2_stubs::strcmp(uint8_t *rdram, R5900Context *ctx)
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

    void ps2_stubs::strncmp(uint8_t *rdram, R5900Context *ctx)
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

    void ps2_stubs::strcat(uint8_t *rdram, R5900Context *ctx)
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

    void ps2_stubs::strncat(uint8_t *rdram, R5900Context *ctx)
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

    void ps2_stubs::strchr(uint8_t *rdram, R5900Context *ctx)
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

    void ps2_stubs::strrchr(uint8_t *rdram, R5900Context *ctx)
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

    void ps2_stubs::strstr(uint8_t *rdram, R5900Context *ctx)
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

    void ps2_stubs::printf(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t format_addr = getRegU32(ctx, 4); // $a0
        const char *format = reinterpret_cast<const char *>(getConstMemPtr(rdram, format_addr));
        int ret = -1;

        if (format)
        {
            // TODO we will Ignores all arguments beyond the format string
            std::cout << "PS2 printf: ";
            ret = std::printf("%s", format); // Just print the format string itself
            std::cout << std::flush;         // Ensure output appears
        }
        else
        {
            std::cerr << "printf error: Invalid format string address provided: 0x" << std::hex << format_addr << std::dec << std::endl;
        }

        // returns the number of characters written, or negative on error.
        setReturnS32(ctx, ret);
    }

    void ps2_stubs::sprintf(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t str_addr = getRegU32(ctx, 4);    // $a0
        uint32_t format_addr = getRegU32(ctx, 5); // $a1

        char *str = reinterpret_cast<char *>(getMemPtr(rdram, str_addr));
        const char *format = reinterpret_cast<const char *>(getConstMemPtr(rdram, format_addr));
        int ret = -1;

        if (str && format)
        {
            // TODO we will Ignores all arguments beyond the format string
            ::strcpy(str, format);
            ret = (int)::strlen(str);
        }
        else
        {
            std::cerr << "sprintf error: Invalid address provided."
                      << " Dest: 0x" << std::hex << str_addr << " (host ptr valid: " << (str != nullptr) << ")"
                      << ", Format: 0x" << format_addr << " (host ptr valid: " << (format != nullptr) << ")" << std::dec
                      << std::endl;
        }

        // returns the number of characters written (excluding null), or negative on error.
        setReturnS32(ctx, ret);
    }

    void ps2_stubs::snprintf(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t str_addr = getRegU32(ctx, 4);    // $a0
        size_t size = getRegU32(ctx, 5);          // $a1
        uint32_t format_addr = getRegU32(ctx, 6); // $a2
        char *str = reinterpret_cast<char *>(getMemPtr(rdram, str_addr));
        const char *format = reinterpret_cast<const char *>(getConstMemPtr(rdram, format_addr));
        int ret = -1;

        if (str && format && size > 0)
        {
            // TODO we will Ignores all arguments beyond the format string

            ::strncpy(str, format, size);
            str[size - 1] = '\0';
            ret = (int)::strlen(str);
        }
        else if (size == 0 && format)
        {
            ret = (int)::strlen(format);
        }
        else
        {
            std::cerr << "snprintf error: Invalid address provided or size is zero."
                      << " Dest: 0x" << std::hex << str_addr << " (host ptr valid: " << (str != nullptr) << ")"
                      << ", Format: 0x" << format_addr << " (host ptr valid: " << (format != nullptr) << ")" << std::dec
                      << ", Size: " << size << std::endl;
        }

        // returns the number of characters that *would* have been written
        // if size was large enough (excluding null), or negative on error.
        setReturnS32(ctx, ret);
    }

    void ps2_stubs::puts(uint8_t *rdram, R5900Context *ctx)
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

    void ps2_stubs::fopen(uint8_t *rdram, R5900Context *ctx)
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

    void ps2_stubs::fclose(uint8_t *rdram, R5900Context *ctx)
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

    void ps2_stubs::fread(uint8_t *rdram, R5900Context *ctx)
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

    void ps2_stubs::fwrite(uint8_t *rdram, R5900Context *ctx)
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

    void ps2_stubs::fprintf(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t file_handle = getRegU32(ctx, 4); // $a0
        uint32_t format_addr = getRegU32(ctx, 5); // $a1
        FILE *fp = get_file_ptr(file_handle);
        const char *format = reinterpret_cast<const char *>(getConstMemPtr(rdram, format_addr));
        int ret = -1;

        if (fp && format)
        {
            // TODO this implementation ignores all arguments beyond the format string
            ret = std::fprintf(fp, "%s", format);
        }
        else
        {
            std::cerr << "fprintf error: Invalid file handle or format address."
                      << " Handle: 0x" << std::hex << file_handle << " (file valid: " << (fp != nullptr) << ")"
                      << ", Format: 0x" << format_addr << " (host ptr valid: " << (format != nullptr) << ")" << std::dec
                      << std::endl;
        }

        // returns the number of characters written, or negative on error.
        setReturnS32(ctx, ret);
    }

    void ps2_stubs::fseek(uint8_t *rdram, R5900Context *ctx)
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

    void ps2_stubs::ftell(uint8_t *rdram, R5900Context *ctx)
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

    void ps2_stubs::fflush(uint8_t *rdram, R5900Context *ctx)
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

    void ps2_stubs::sqrt(uint8_t *rdram, R5900Context *ctx)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::sqrtf(arg);
    }

    void ps2_stubs::sin(uint8_t *rdram, R5900Context *ctx)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::sinf(arg);
    }

    void ps2_stubs::cos(uint8_t *rdram, R5900Context *ctx)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::cosf(arg);
    }

    void ps2_stubs::tan(uint8_t *rdram, R5900Context *ctx)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::tanf(arg);
    }

    void ps2_stubs::atan2(uint8_t *rdram, R5900Context *ctx)
    {
        float y = ctx->f[12];
        float x = ctx->f[14];
        ctx->f[0] = ::atan2f(y, x);
    }

    void ps2_stubs::pow(uint8_t *rdram, R5900Context *ctx)
    {
        float base = ctx->f[12];
        float exp = ctx->f[14];
        ctx->f[0] = ::powf(base, exp);
    }

    void ps2_stubs::exp(uint8_t *rdram, R5900Context *ctx)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::expf(arg);
    }

    void ps2_stubs::log(uint8_t *rdram, R5900Context *ctx)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::logf(arg);
    }

    void ps2_stubs::log10(uint8_t *rdram, R5900Context *ctx)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::log10f(arg);
    }

    void ps2_stubs::ceil(uint8_t *rdram, R5900Context *ctx)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::ceilf(arg);
    }

    void ps2_stubs::floor(uint8_t *rdram, R5900Context *ctx)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::floorf(arg);
    }

    void ps2_stubs::fabs(uint8_t *rdram, R5900Context *ctx)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::fabsf(arg);
    }

    void ps2_stubs::TODO(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t stub_num = getRegU32(ctx, 2);   // $v0 often holds stub num *before* call
        uint32_t caller_ra = getRegU32(ctx, 31); // $ra

        std::cerr << "Warning: Unimplemented PS2 stub called. PC=0x" << std::hex << ctx->pc
                  << ", RA=0x" << caller_ra
                  << ", Stub# guess (from $v0)=0x" << stub_num << std::dec << std::endl;

        // More context for debugging
        std::cerr << "  Args: $a0=0x" << std::hex << getRegU32(ctx, 4)
                  << ", $a1=0x" << getRegU32(ctx, 5)
                  << ", $a2=0x" << getRegU32(ctx, 6)
                  << ", $a3=0x" << getRegU32(ctx, 7) << std::dec << std::endl;

        setReturnS32(ctx, -1); // Return error
    }
}