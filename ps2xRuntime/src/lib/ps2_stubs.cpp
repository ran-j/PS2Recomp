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

    void malloc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
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

    void free(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
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

    void calloc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
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

    void realloc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
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

    void sprintf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
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

    void snprintf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
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

    void cos(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        float arg = ctx->f[12];
        ctx->f[0] = ::cosf(arg);
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

    void sceCdRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t lbn = getRegU32(ctx, 4);     // $a0 - logical block number
        uint32_t sectors = getRegU32(ctx, 5); // $a1 - sector count
        uint32_t buf = getRegU32(ctx, 6);     // $a2 - destination buffer in RDRAM

        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub sceCdRead: lbn=0x" << std::hex << lbn
                      << " sectors=" << std::dec << sectors
                      << " buf=0x" << std::hex << buf << std::dec << std::endl;
            ++logCount;
        }

        size_t bytes = static_cast<size_t>(sectors) * 2048; // CD/DVD sector size
        if (bytes > 0)
        {
            uint32_t offset = buf & PS2_RAM_MASK;
            size_t maxBytes = PS2_RAM_SIZE - offset;
            if (bytes > maxBytes)
                bytes = maxBytes;
            std::memset(rdram + offset, 0, bytes);
        }

        setReturnS32(ctx, 1); // Success
    }

    void sceCdSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub sceCdSync" << std::endl;
            ++logCount;
        }

        setReturnS32(ctx, 0); // 0 = completed/not busy
    }

    void sceCdGetError(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub sceCdGetError" << std::endl;
            ++logCount;
        }

        setReturnS32(ctx, 0); // no error
    }

    void njSetBorderColor(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub njSetBorderColor" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void njSetTextureMemorySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub njSetTextureMemorySize" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void njInitVertexBuffer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub njInitVertexBuffer" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void njTextureShadingMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub njTextureShadingMode" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void njInitView(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub njInitView" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void njSetAspect(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub njSetAspect" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void njInitSystem(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub njInitSystem" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void njInitPrint(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub njInitPrint" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void njPolygonCullingMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub njPolygonCullingMode" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void njSetView(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub njSetView" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void njGetMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub njGetMatrix" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void njInitTexture(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub njInitTexture" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void njInitTextureBuffer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub njInitTextureBuffer" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void njSetPaletteMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub njSetPaletteMode" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void njClipZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub njClipZ" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void syRtcInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub syRtcInit" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void _builtin_set_imask(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub _builtin_set_imask" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void syFree(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub syFree" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void InitSdcParameter(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub InitSdcParameter" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void Ps2_pad_actuater(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub Ps2_pad_actuater" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void syMallocInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub syMallocInit" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void syHwInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub syHwInit" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void syHwInit2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub syHwInit2" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void InitGdSystemEx(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub InitGdSystemEx" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void pdInitPeripheral(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub pdInitPeripheral" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void njSetVertexBuffer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub njSetVertexBuffer" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void njPrintSize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub njPrintSize" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void pdGetPeripheral(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub pdGetPeripheral" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void Ps2SwapDBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub Ps2SwapDBuff" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void InitReadKeyEx(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub InitReadKeyEx" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void SetRepeatKeyTimer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub SetRepeatKeyTimer" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void StopFxProgram(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub StopFxProgram" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void sndr_trans_func(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub sndr_trans_func (noop)" << std::endl;
            ++logCount;
        }

        // For now just clear the snd busy flag used by sdMultiUnitDownload/SysServer loops.
        constexpr uint32_t kSndBusyAddr = 0x01E0E170;
        if (rdram)
        {
            uint32_t offset = kSndBusyAddr & PS2_RAM_MASK;
            if (offset + sizeof(uint32_t) <= PS2_RAM_SIZE)
            {
                *reinterpret_cast<uint32_t *>(rdram + offset) = 0;
            }
        }

        setReturnS32(ctx, 0);
    }

    void sdDrvInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub sdDrvInit (noop)" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void ADXF_LoadPartitionNw(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub ADXF_LoadPartitionNw (noop)" << std::endl;
            ++logCount;
        }
        // Return success to keep the ADX partition setup moving.
        setReturnS32(ctx, 0);
    }

    void sdSndStopAll(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub sdSndStopAll" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void sdSysFinish(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub sdSysFinish" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void ADXT_Init(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub ADXT_Init" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void ADXT_SetNumRetry(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub ADXT_SetNumRetry" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void cvFsSetDefDev(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            std::cout << "ps2_stub cvFsSetDefDev" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void _calloc_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_calloc_r", rdram, ctx, runtime);
    }

    void _free_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_free_r", rdram, ctx, runtime);
    }

    void _malloc_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_malloc_r", rdram, ctx, runtime);
    }

    void _malloc_trim_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_malloc_trim_r", rdram, ctx, runtime);
    }

    void _mbtowc_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_mbtowc_r", rdram, ctx, runtime);
    }

    void _printf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_printf", rdram, ctx, runtime);
    }

    void _printf_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_printf_r", rdram, ctx, runtime);
    }

    void _sceCdRI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceCdRI", rdram, ctx, runtime);
    }

    void _sceCdRM(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceCdRM", rdram, ctx, runtime);
    }

    void _sceFsDbChk(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceFsDbChk", rdram, ctx, runtime);
    }

    void _sceFsIntrSigSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceFsIntrSigSema", rdram, ctx, runtime);
    }

    void _sceFsSemExit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceFsSemExit", rdram, ctx, runtime);
    }

    void _sceFsSemInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceFsSemInit", rdram, ctx, runtime);
    }

    void _sceFsSigSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceFsSigSema", rdram, ctx, runtime);
    }

    void _sceIDC(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceIDC", rdram, ctx, runtime);
    }

    void _sceMpegFlush(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceMpegFlush", rdram, ctx, runtime);
    }

    void _sceRpcFreePacket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceRpcFreePacket", rdram, ctx, runtime);
    }

    void _sceRpcGetFPacket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceRpcGetFPacket", rdram, ctx, runtime);
    }

    void _sceRpcGetFPacket2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceRpcGetFPacket2", rdram, ctx, runtime);
    }

    void _sceSDC(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceSDC", rdram, ctx, runtime);
    }

    void _sceSifCmdIntrHdlr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceSifCmdIntrHdlr", rdram, ctx, runtime);
    }

    void _sceSifLoadElfPart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceSifLoadElfPart", rdram, ctx, runtime);
    }

    void _sceSifLoadModule(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceSifLoadModule", rdram, ctx, runtime);
    }

    void _sceSifSendCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceSifSendCmd", rdram, ctx, runtime);
    }

    void _sceVu0ecossin(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("_sceVu0ecossin", rdram, ctx, runtime);
    }

    void abs(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("abs", rdram, ctx, runtime);
    }

    void atan(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("atan", rdram, ctx, runtime);
    }

    void close(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("close", rdram, ctx, runtime);
    }

    void DmaAddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("DmaAddr", rdram, ctx, runtime);
    }

    void exit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("exit", rdram, ctx, runtime);
    }

    void fstat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("fstat", rdram, ctx, runtime);
    }

    void getpid(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("getpid", rdram, ctx, runtime);
    }

    void iopGetArea(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("iopGetArea", rdram, ctx, runtime);
    }

    void lseek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("lseek", rdram, ctx, runtime);
    }

    void mcCallMessageTypeSe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcCallMessageTypeSe", rdram, ctx, runtime);
    }

    void mcCheckReadStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcCheckReadStartConfigFile", rdram, ctx, runtime);
    }

    void mcCheckReadStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcCheckReadStartSaveFile", rdram, ctx, runtime);
    }

    void mcCheckWriteStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcCheckWriteStartConfigFile", rdram, ctx, runtime);
    }

    void mcCheckWriteStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcCheckWriteStartSaveFile", rdram, ctx, runtime);
    }

    void mcCreateConfigInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcCreateConfigInit", rdram, ctx, runtime);
    }

    void mcCreateFileSelectWindow(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcCreateFileSelectWindow", rdram, ctx, runtime);
    }

    void mcCreateIconInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcCreateIconInit", rdram, ctx, runtime);
    }

    void mcCreateSaveFileInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcCreateSaveFileInit", rdram, ctx, runtime);
    }

    void mcDispFileName(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcDispFileName", rdram, ctx, runtime);
    }

    void mcDispFileNumber(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcDispFileNumber", rdram, ctx, runtime);
    }

    void mcDisplayFileSelectWindow(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcDisplayFileSelectWindow", rdram, ctx, runtime);
    }

    void mcDisplaySelectFileInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcDisplaySelectFileInfo", rdram, ctx, runtime);
    }

    void mcDisplaySelectFileInfoMesCount(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcDisplaySelectFileInfoMesCount", rdram, ctx, runtime);
    }

    void mcDispWindowCurSol(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcDispWindowCurSol", rdram, ctx, runtime);
    }

    void mcDispWindowFoundtion(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcDispWindowFoundtion", rdram, ctx, runtime);
    }

    void mceGetInfoApdx(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mceGetInfoApdx", rdram, ctx, runtime);
    }

    void mceIntrReadFixAlign(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mceIntrReadFixAlign", rdram, ctx, runtime);
    }

    void mceStorePwd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mceStorePwd", rdram, ctx, runtime);
    }

    void mcGetConfigCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcGetConfigCapacitySize", rdram, ctx, runtime);
    }

    void mcGetFileSelectWindowCursol(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcGetFileSelectWindowCursol", rdram, ctx, runtime);
    }

    void mcGetFreeCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcGetFreeCapacitySize", rdram, ctx, runtime);
    }

    void mcGetIconCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcGetIconCapacitySize", rdram, ctx, runtime);
    }

    void mcGetIconFileCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcGetIconFileCapacitySize", rdram, ctx, runtime);
    }

    void mcGetPortSelectDirInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcGetPortSelectDirInfo", rdram, ctx, runtime);
    }

    void mcGetSaveFileCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcGetSaveFileCapacitySize", rdram, ctx, runtime);
    }

    void mcGetStringEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcGetStringEnd", rdram, ctx, runtime);
    }

    void mcMoveFileSelectWindowCursor(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcMoveFileSelectWindowCursor", rdram, ctx, runtime);
    }

    void mcNewCreateConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcNewCreateConfigFile", rdram, ctx, runtime);
    }

    void mcNewCreateIcon(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcNewCreateIcon", rdram, ctx, runtime);
    }

    void mcNewCreateSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcNewCreateSaveFile", rdram, ctx, runtime);
    }

    void mcReadIconData(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcReadIconData", rdram, ctx, runtime);
    }

    void mcReadStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcReadStartConfigFile", rdram, ctx, runtime);
    }

    void mcReadStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcReadStartSaveFile", rdram, ctx, runtime);
    }

    void mcSelectFileInfoInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcSelectFileInfoInit", rdram, ctx, runtime);
    }

    void mcSelectSaveFileCheck(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcSelectSaveFileCheck", rdram, ctx, runtime);
    }

    void mcSetFileSelectWindowCursol(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcSetFileSelectWindowCursol", rdram, ctx, runtime);
    }

    void mcSetFileSelectWindowCursolInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcSetFileSelectWindowCursolInit", rdram, ctx, runtime);
    }

    void mcSetStringSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcSetStringSaveFile", rdram, ctx, runtime);
    }

    void mcSetTyepWriteMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcSetTyepWriteMode", rdram, ctx, runtime);
    }

    void mcWriteIconData(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcWriteIconData", rdram, ctx, runtime);
    }

    void mcWriteStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcWriteStartConfigFile", rdram, ctx, runtime);
    }

    void mcWriteStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("mcWriteStartSaveFile", rdram, ctx, runtime);
    }

    void memchr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("memchr", rdram, ctx, runtime);
    }

    void open(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("open", rdram, ctx, runtime);
    }

    void Pad_init(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("Pad_init", rdram, ctx, runtime);
    }

    void Pad_set(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("Pad_set", rdram, ctx, runtime);
    }

    void rand(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("rand", rdram, ctx, runtime);
    }

    void read(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("read", rdram, ctx, runtime);
    }

    void sceCdApplyNCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdApplyNCmd", rdram, ctx, runtime);
    }

    void sceCdBreak(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdBreak", rdram, ctx, runtime);
    }

    void sceCdCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdCallback", rdram, ctx, runtime);
    }

    void sceCdChangeThreadPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdChangeThreadPriority", rdram, ctx, runtime);
    }

    void sceCdDelayThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdDelayThread", rdram, ctx, runtime);
    }

    void sceCdDiskReady(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdDiskReady", rdram, ctx, runtime);
    }

    void sceCdGetDiskType(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdGetDiskType", rdram, ctx, runtime);
    }

    void sceCdGetReadPos(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdGetReadPos", rdram, ctx, runtime);
    }

    void sceCdGetToc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdGetToc", rdram, ctx, runtime);
    }

    void sceCdInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdInit", rdram, ctx, runtime);
    }

    void sceCdInitEeCB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdInitEeCB", rdram, ctx, runtime);
    }

    void sceCdIntToPos(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdIntToPos", rdram, ctx, runtime);
    }

    void sceCdMmode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdMmode", rdram, ctx, runtime);
    }

    void sceCdNcmdDiskReady(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdNcmdDiskReady", rdram, ctx, runtime);
    }

    void sceCdPause(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdPause", rdram, ctx, runtime);
    }

    void sceCdPosToInt(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdPosToInt", rdram, ctx, runtime);
    }

    void sceCdReadChain(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdReadChain", rdram, ctx, runtime);
    }

    void sceCdReadClock(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdReadClock", rdram, ctx, runtime);
    }

    void sceCdReadIOPm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdReadIOPm", rdram, ctx, runtime);
    }

    void sceCdSearchFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdSearchFile", rdram, ctx, runtime);
    }

    void sceCdSeek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdSeek", rdram, ctx, runtime);
    }

    void sceCdStandby(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdStandby", rdram, ctx, runtime);
    }

    void sceCdStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdStatus", rdram, ctx, runtime);
    }

    void sceCdStInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdStInit", rdram, ctx, runtime);
    }

    void sceCdStop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdStop", rdram, ctx, runtime);
    }

    void sceCdStPause(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdStPause", rdram, ctx, runtime);
    }

    void sceCdStRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdStRead", rdram, ctx, runtime);
    }

    void sceCdStream(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdStream", rdram, ctx, runtime);
    }

    void sceCdStResume(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdStResume", rdram, ctx, runtime);
    }

    void sceCdStSeek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdStSeek", rdram, ctx, runtime);
    }

    void sceCdStSeekF(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdStSeekF", rdram, ctx, runtime);
    }

    void sceCdStStart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdStStart", rdram, ctx, runtime);
    }

    void sceCdStStat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdStStat", rdram, ctx, runtime);
    }

    void sceCdStStop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdStStop", rdram, ctx, runtime);
    }

    void sceCdSyncS(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdSyncS", rdram, ctx, runtime);
    }

    void sceCdTrayReq(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceCdTrayReq", rdram, ctx, runtime);
    }

    void sceClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceClose", rdram, ctx, runtime);
    }

    void sceDeci2Close(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDeci2Close", rdram, ctx, runtime);
    }

    void sceDeci2ExLock(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDeci2ExLock", rdram, ctx, runtime);
    }

    void sceDeci2ExRecv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDeci2ExRecv", rdram, ctx, runtime);
    }

    void sceDeci2ExReqSend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDeci2ExReqSend", rdram, ctx, runtime);
    }

    void sceDeci2ExSend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDeci2ExSend", rdram, ctx, runtime);
    }

    void sceDeci2ExUnLock(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDeci2ExUnLock", rdram, ctx, runtime);
    }

    void sceDeci2Open(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDeci2Open", rdram, ctx, runtime);
    }

    void sceDeci2Poll(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDeci2Poll", rdram, ctx, runtime);
    }

    void sceDeci2ReqSend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDeci2ReqSend", rdram, ctx, runtime);
    }

    void sceDmaCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaCallback", rdram, ctx, runtime);
    }

    void sceDmaDebug(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaDebug", rdram, ctx, runtime);
    }

    void sceDmaGetChan(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaGetChan", rdram, ctx, runtime);
    }

    void sceDmaGetEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaGetEnv", rdram, ctx, runtime);
    }

    void sceDmaLastSyncTime(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaLastSyncTime", rdram, ctx, runtime);
    }

    void sceDmaPause(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaPause", rdram, ctx, runtime);
    }

    void sceDmaPutEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaPutEnv", rdram, ctx, runtime);
    }

    void sceDmaPutStallAddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaPutStallAddr", rdram, ctx, runtime);
    }

    void sceDmaRecv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaRecv", rdram, ctx, runtime);
    }

    void sceDmaRecvI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaRecvI", rdram, ctx, runtime);
    }

    void sceDmaRecvN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaRecvN", rdram, ctx, runtime);
    }

    void sceDmaReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaReset", rdram, ctx, runtime);
    }

    void sceDmaRestart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaRestart", rdram, ctx, runtime);
    }

    void sceDmaSend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaSend", rdram, ctx, runtime);
    }

    void sceDmaSendI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaSendI", rdram, ctx, runtime);
    }

    void sceDmaSendM(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaSendM", rdram, ctx, runtime);
    }

    void sceDmaSendN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaSendN", rdram, ctx, runtime);
    }

    void sceDmaSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaSync", rdram, ctx, runtime);
    }

    void sceDmaSyncN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaSyncN", rdram, ctx, runtime);
    }

    void sceDmaWatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceDmaWatch", rdram, ctx, runtime);
    }

    void sceFsInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceFsInit", rdram, ctx, runtime);
    }

    void sceFsReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceFsReset", rdram, ctx, runtime);
    }

    void sceGsExecLoadImage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsExecLoadImage", rdram, ctx, runtime);
    }

    void sceGsExecStoreImage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsExecStoreImage", rdram, ctx, runtime);
    }

    void sceGsGetGParam(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsGetGParam", rdram, ctx, runtime);
    }

    void sceGsPutDispEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsPutDispEnv", rdram, ctx, runtime);
    }

    void sceGsPutDrawEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsPutDrawEnv", rdram, ctx, runtime);
    }

    void sceGsResetGraph(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsResetGraph", rdram, ctx, runtime);
    }

    void sceGsResetPath(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsResetPath", rdram, ctx, runtime);
    }

    void sceGsSetDefClear(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsSetDefClear", rdram, ctx, runtime);
    }

    void sceGsSetDefDBuffDc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsSetDefDBuffDc", rdram, ctx, runtime);
    }

    void sceGsSetDefDispEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsSetDefDispEnv", rdram, ctx, runtime);
    }

    void sceGsSetDefDrawEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsSetDefDrawEnv", rdram, ctx, runtime);
    }

    void sceGsSetDefDrawEnv2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsSetDefDrawEnv2", rdram, ctx, runtime);
    }

    void sceGsSetDefLoadImage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsSetDefLoadImage", rdram, ctx, runtime);
    }

    void sceGsSetDefStoreImage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsSetDefStoreImage", rdram, ctx, runtime);
    }

    void sceGsSwapDBuffDc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsSwapDBuffDc", rdram, ctx, runtime);
    }

    void sceGsSyncPath(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsSyncPath", rdram, ctx, runtime);
    }

    void sceGsSyncV(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsSyncV", rdram, ctx, runtime);
    }

    void sceGsSyncVCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGsSyncVCallback", rdram, ctx, runtime);
    }

    void sceGszbufaddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceGszbufaddr", rdram, ctx, runtime);
    }

    void sceIoctl(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceIoctl", rdram, ctx, runtime);
    }

    void sceIpuInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceIpuInit", rdram, ctx, runtime);
    }

    void sceIpuRestartDMA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceIpuRestartDMA", rdram, ctx, runtime);
    }

    void sceIpuStopDMA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceIpuStopDMA", rdram, ctx, runtime);
    }

    void sceIpuSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceIpuSync", rdram, ctx, runtime);
    }

    void sceLseek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceLseek", rdram, ctx, runtime);
    }

    void sceMcChangeThreadPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcChangeThreadPriority", rdram, ctx, runtime);
    }

    void sceMcChdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcChdir", rdram, ctx, runtime);
    }

    void sceMcClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcClose", rdram, ctx, runtime);
    }

    void sceMcDelete(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcDelete", rdram, ctx, runtime);
    }

    void sceMcFlush(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcFlush", rdram, ctx, runtime);
    }

    void sceMcFormat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcFormat", rdram, ctx, runtime);
    }

    void sceMcGetDir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcGetDir", rdram, ctx, runtime);
    }

    void sceMcGetEntSpace(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcGetEntSpace", rdram, ctx, runtime);
    }

    void sceMcGetInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcGetInfo", rdram, ctx, runtime);
    }

    void sceMcGetSlotMax(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcGetSlotMax", rdram, ctx, runtime);
    }

    void sceMcInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcInit", rdram, ctx, runtime);
    }

    void sceMcMkdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcMkdir", rdram, ctx, runtime);
    }

    void sceMcOpen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcOpen", rdram, ctx, runtime);
    }

    void sceMcRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcRead", rdram, ctx, runtime);
    }

    void sceMcRename(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcRename", rdram, ctx, runtime);
    }

    void sceMcSeek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcSeek", rdram, ctx, runtime);
    }

    void sceMcSetFileInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcSetFileInfo", rdram, ctx, runtime);
    }

    void sceMcSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcSync", rdram, ctx, runtime);
    }

    void sceMcUnformat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcUnformat", rdram, ctx, runtime);
    }

    void sceMcWrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMcWrite", rdram, ctx, runtime);
    }

    void sceMpegAddBs(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegAddBs", rdram, ctx, runtime);
    }

    void sceMpegAddCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegAddCallback", rdram, ctx, runtime);
    }

    void sceMpegAddStrCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegAddStrCallback", rdram, ctx, runtime);
    }

    void sceMpegClearRefBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegClearRefBuff", rdram, ctx, runtime);
    }

    void sceMpegCreate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegCreate", rdram, ctx, runtime);
    }

    void sceMpegDelete(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegDelete", rdram, ctx, runtime);
    }

    void sceMpegDemuxPss(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegDemuxPss", rdram, ctx, runtime);
    }

    void sceMpegDemuxPssRing(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegDemuxPssRing", rdram, ctx, runtime);
    }

    void sceMpegDispCenterOffX(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegDispCenterOffX", rdram, ctx, runtime);
    }

    void sceMpegDispCenterOffY(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegDispCenterOffY", rdram, ctx, runtime);
    }

    void sceMpegDispHeight(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegDispHeight", rdram, ctx, runtime);
    }

    void sceMpegDispWidth(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegDispWidth", rdram, ctx, runtime);
    }

    void sceMpegGetDecodeMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegGetDecodeMode", rdram, ctx, runtime);
    }

    void sceMpegGetPicture(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegGetPicture", rdram, ctx, runtime);
    }

    void sceMpegGetPictureRAW8(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegGetPictureRAW8", rdram, ctx, runtime);
    }

    void sceMpegGetPictureRAW8xy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegGetPictureRAW8xy", rdram, ctx, runtime);
    }

    void sceMpegInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegInit", rdram, ctx, runtime);
    }

    void sceMpegIsEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegIsEnd", rdram, ctx, runtime);
    }

    void sceMpegIsRefBuffEmpty(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegIsRefBuffEmpty", rdram, ctx, runtime);
    }

    void sceMpegReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegReset", rdram, ctx, runtime);
    }

    void sceMpegResetDefaultPtsGap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegResetDefaultPtsGap", rdram, ctx, runtime);
    }

    void sceMpegSetDecodeMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegSetDecodeMode", rdram, ctx, runtime);
    }

    void sceMpegSetDefaultPtsGap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegSetDefaultPtsGap", rdram, ctx, runtime);
    }

    void sceMpegSetImageBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegSetImageBuff", rdram, ctx, runtime);
    }

    void sceOpen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceOpen", rdram, ctx, runtime);
    }

    void scePadEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadEnd", rdram, ctx, runtime);
    }

    void scePadEnterPressMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadEnterPressMode", rdram, ctx, runtime);
    }

    void scePadExitPressMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadExitPressMode", rdram, ctx, runtime);
    }

    void scePadGetButtonMask(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadGetButtonMask", rdram, ctx, runtime);
    }

    void scePadGetDmaStr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadGetDmaStr", rdram, ctx, runtime);
    }

    void scePadGetFrameCount(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadGetFrameCount", rdram, ctx, runtime);
    }

    void scePadGetModVersion(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadGetModVersion", rdram, ctx, runtime);
    }

    void scePadGetPortMax(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadGetPortMax", rdram, ctx, runtime);
    }

    void scePadGetReqState(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadGetReqState", rdram, ctx, runtime);
    }

    void scePadGetSlotMax(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadGetSlotMax", rdram, ctx, runtime);
    }

    void scePadGetState(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadGetState", rdram, ctx, runtime);
    }

    void scePadInfoAct(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadInfoAct", rdram, ctx, runtime);
    }

    void scePadInfoComb(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadInfoComb", rdram, ctx, runtime);
    }

    void scePadInfoMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadInfoMode", rdram, ctx, runtime);
    }

    void scePadInfoPressMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadInfoPressMode", rdram, ctx, runtime);
    }

    void scePadInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadInit", rdram, ctx, runtime);
    }

    void scePadInit2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadInit2", rdram, ctx, runtime);
    }

    void scePadPortClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadPortClose", rdram, ctx, runtime);
    }

    void scePadPortOpen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadPortOpen", rdram, ctx, runtime);
    }

    void scePadRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadRead", rdram, ctx, runtime);
    }

    void scePadReqIntToStr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadReqIntToStr", rdram, ctx, runtime);
    }

    void scePadSetActAlign(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadSetActAlign", rdram, ctx, runtime);
    }

    void scePadSetActDirect(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadSetActDirect", rdram, ctx, runtime);
    }

    void scePadSetButtonInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadSetButtonInfo", rdram, ctx, runtime);
    }

    void scePadSetMainMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadSetMainMode", rdram, ctx, runtime);
    }

    void scePadSetReqState(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadSetReqState", rdram, ctx, runtime);
    }

    void scePadSetVrefParam(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadSetVrefParam", rdram, ctx, runtime);
    }

    void scePadSetWarningLevel(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadSetWarningLevel", rdram, ctx, runtime);
    }

    void scePadStateIntToStr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePadStateIntToStr", rdram, ctx, runtime);
    }

    void scePrintf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("scePrintf", rdram, ctx, runtime);
    }

    void sceRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceRead", rdram, ctx, runtime);
    }

    void sceResetttyinit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceResetttyinit", rdram, ctx, runtime);
    }

    void sceSdCallBack(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSdCallBack", rdram, ctx, runtime);
    }

    void sceSdRemote(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSdRemote", rdram, ctx, runtime);
    }

    void sceSdRemoteInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSdRemoteInit", rdram, ctx, runtime);
    }

    void sceSdTransToIOP(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSdTransToIOP", rdram, ctx, runtime);
    }

    void sceSetBrokenLink(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSetBrokenLink", rdram, ctx, runtime);
    }

    void sceSetPtm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSetPtm", rdram, ctx, runtime);
    }

    void sceSifAddCmdHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifAddCmdHandler", rdram, ctx, runtime);
    }

    void sceSifAllocIopHeap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifAllocIopHeap", rdram, ctx, runtime);
    }

    void sceSifBindRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifBindRpc", rdram, ctx, runtime);
    }

    void sceSifCheckStatRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifCheckStatRpc", rdram, ctx, runtime);
    }

    void sceSifDmaStat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifDmaStat", rdram, ctx, runtime);
    }

    void sceSifExecRequest(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifExecRequest", rdram, ctx, runtime);
    }

    void sceSifExitCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifExitCmd", rdram, ctx, runtime);
    }

    void sceSifExitRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifExitRpc", rdram, ctx, runtime);
    }

    void sceSifFreeIopHeap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifFreeIopHeap", rdram, ctx, runtime);
    }

    void sceSifGetDataTable(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifGetDataTable", rdram, ctx, runtime);
    }

    void sceSifGetIopAddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifGetIopAddr", rdram, ctx, runtime);
    }

    void sceSifGetNextRequest(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifGetNextRequest", rdram, ctx, runtime);
    }

    void sceSifGetOtherData(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifGetOtherData", rdram, ctx, runtime);
    }

    void sceSifGetReg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifGetReg", rdram, ctx, runtime);
    }

    void sceSifGetSreg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifGetSreg", rdram, ctx, runtime);
    }

    void sceSifInitCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifInitCmd", rdram, ctx, runtime);
    }

    void sceSifInitIopHeap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifInitIopHeap", rdram, ctx, runtime);
    }

    void sceSifInitRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifInitRpc", rdram, ctx, runtime);
    }

    void sceSifIsAliveIop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifIsAliveIop", rdram, ctx, runtime);
    }

    void sceSifLoadElf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifLoadElf", rdram, ctx, runtime);
    }

    void sceSifLoadElfPart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifLoadElfPart", rdram, ctx, runtime);
    }

    void sceSifLoadFileReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifLoadFileReset", rdram, ctx, runtime);
    }

    void sceSifLoadIopHeap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifLoadIopHeap", rdram, ctx, runtime);
    }

    void sceSifLoadModuleBuffer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifLoadModuleBuffer", rdram, ctx, runtime);
    }

    void sceSifRebootIop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifRebootIop", rdram, ctx, runtime);
    }

    void sceSifRegisterRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifRegisterRpc", rdram, ctx, runtime);
    }

    void sceSifRemoveCmdHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifRemoveCmdHandler", rdram, ctx, runtime);
    }

    void sceSifRemoveRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifRemoveRpc", rdram, ctx, runtime);
    }

    void sceSifRemoveRpcQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifRemoveRpcQueue", rdram, ctx, runtime);
    }

    void sceSifResetIop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifResetIop", rdram, ctx, runtime);
    }

    void sceSifRpcLoop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifRpcLoop", rdram, ctx, runtime);
    }

    void sceSifSetCmdBuffer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifSetCmdBuffer", rdram, ctx, runtime);
    }

    void sceSifSetDChain(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifSetDChain", rdram, ctx, runtime);
    }

    void sceSifSetDma(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifSetDma", rdram, ctx, runtime);
    }

    void sceSifSetIopAddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifSetIopAddr", rdram, ctx, runtime);
    }

    void sceSifSetReg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifSetReg", rdram, ctx, runtime);
    }

    void sceSifSetRpcQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifSetRpcQueue", rdram, ctx, runtime);
    }

    void sceSifSetSreg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifSetSreg", rdram, ctx, runtime);
    }

    void sceSifSetSysCmdBuffer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifSetSysCmdBuffer", rdram, ctx, runtime);
    }

    void sceSifStopDma(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifStopDma", rdram, ctx, runtime);
    }

    void sceSifSyncIop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifSyncIop", rdram, ctx, runtime);
    }

    void sceSifWriteBackDCache(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSifWriteBackDCache", rdram, ctx, runtime);
    }

    void sceSSyn_BreakAtick(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_BreakAtick", rdram, ctx, runtime);
    }

    void sceSSyn_ClearBreakAtick(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_ClearBreakAtick", rdram, ctx, runtime);
    }

    void sceSSyn_SendExcMsg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SendExcMsg", rdram, ctx, runtime);
    }

    void sceSSyn_SendNrpnMsg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SendNrpnMsg", rdram, ctx, runtime);
    }

    void sceSSyn_SendRpnMsg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SendRpnMsg", rdram, ctx, runtime);
    }

    void sceSSyn_SendShortMsg(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SendShortMsg", rdram, ctx, runtime);
    }

    void sceSSyn_SetChPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SetChPriority", rdram, ctx, runtime);
    }

    void sceSSyn_SetMasterVolume(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SetMasterVolume", rdram, ctx, runtime);
    }

    void sceSSyn_SetOutPortVolume(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SetOutPortVolume", rdram, ctx, runtime);
    }

    void sceSSyn_SetOutputAssign(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SetOutputAssign", rdram, ctx, runtime);
    }

    void sceSSyn_SetOutputMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SetOutputMode", rdram, ctx, runtime);
    }

    void sceSSyn_SetPortMaxPoly(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SetPortMaxPoly", rdram, ctx, runtime);
    }

    void sceSSyn_SetPortVolume(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SetPortVolume", rdram, ctx, runtime);
    }

    void sceSSyn_SetTvaEnvMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSSyn_SetTvaEnvMode", rdram, ctx, runtime);
    }

    void sceSynthesizerAmpProcI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerAmpProcI", rdram, ctx, runtime);
    }

    void sceSynthesizerAmpProcNI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerAmpProcNI", rdram, ctx, runtime);
    }

    void sceSynthesizerAssignAllNoteOff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerAssignAllNoteOff", rdram, ctx, runtime);
    }

    void sceSynthesizerAssignAllSoundOff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerAssignAllSoundOff", rdram, ctx, runtime);
    }

    void sceSynthesizerAssignHoldChange(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerAssignHoldChange", rdram, ctx, runtime);
    }

    void sceSynthesizerAssignNoteOff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerAssignNoteOff", rdram, ctx, runtime);
    }

    void sceSynthesizerAssignNoteOn(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerAssignNoteOn", rdram, ctx, runtime);
    }

    void sceSynthesizerCalcEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerCalcEnv", rdram, ctx, runtime);
    }

    void sceSynthesizerCalcPortamentPitch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerCalcPortamentPitch", rdram, ctx, runtime);
    }

    void sceSynthesizerCalcTvfCoefAll(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerCalcTvfCoefAll", rdram, ctx, runtime);
    }

    void sceSynthesizerCalcTvfCoefF0(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerCalcTvfCoefF0", rdram, ctx, runtime);
    }

    void sceSynthesizerCent2PhaseInc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerCent2PhaseInc", rdram, ctx, runtime);
    }

    void sceSynthesizerChangeEffectSend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangeEffectSend", rdram, ctx, runtime);
    }

    void sceSynthesizerChangeHsPanpot(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangeHsPanpot", rdram, ctx, runtime);
    }

    void sceSynthesizerChangeNrpnCutOff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangeNrpnCutOff", rdram, ctx, runtime);
    }

    void sceSynthesizerChangeNrpnLfoDepth(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangeNrpnLfoDepth", rdram, ctx, runtime);
    }

    void sceSynthesizerChangeNrpnLfoRate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangeNrpnLfoRate", rdram, ctx, runtime);
    }

    void sceSynthesizerChangeOutAttrib(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangeOutAttrib", rdram, ctx, runtime);
    }

    void sceSynthesizerChangeOutVol(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangeOutVol", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePanpot(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePanpot", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePartBendSens(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePartBendSens", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePartExpression(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePartExpression", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePartHsExpression(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePartHsExpression", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePartHsPitchBend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePartHsPitchBend", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePartModuration(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePartModuration", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePartPitchBend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePartPitchBend", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePartVolume(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePartVolume", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePortamento(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePortamento", rdram, ctx, runtime);
    }

    void sceSynthesizerChangePortamentoTime(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerChangePortamentoTime", rdram, ctx, runtime);
    }

    void sceSynthesizerClearKeyMap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerClearKeyMap", rdram, ctx, runtime);
    }

    void sceSynthesizerClearSpr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerClearSpr", rdram, ctx, runtime);
    }

    void sceSynthesizerCopyOutput(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerCopyOutput", rdram, ctx, runtime);
    }

    void sceSynthesizerDmaFromSPR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerDmaFromSPR", rdram, ctx, runtime);
    }

    void sceSynthesizerDmaSpr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerDmaSpr", rdram, ctx, runtime);
    }

    void sceSynthesizerDmaToSPR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerDmaToSPR", rdram, ctx, runtime);
    }

    void sceSynthesizerGetPartial(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerGetPartial", rdram, ctx, runtime);
    }

    void sceSynthesizerGetPartOutLevel(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerGetPartOutLevel", rdram, ctx, runtime);
    }

    void sceSynthesizerGetSampleParam(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerGetSampleParam", rdram, ctx, runtime);
    }

    void sceSynthesizerHsMessage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerHsMessage", rdram, ctx, runtime);
    }

    void sceSynthesizerLfoNone(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerLfoNone", rdram, ctx, runtime);
    }

    void sceSynthesizerLfoProc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerLfoProc", rdram, ctx, runtime);
    }

    void sceSynthesizerLfoSawDown(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerLfoSawDown", rdram, ctx, runtime);
    }

    void sceSynthesizerLfoSawUp(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerLfoSawUp", rdram, ctx, runtime);
    }

    void sceSynthesizerLfoSquare(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerLfoSquare", rdram, ctx, runtime);
    }

    void sceSynthesizerReadNoise(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerReadNoise", rdram, ctx, runtime);
    }

    void sceSynthesizerReadNoiseAdd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerReadNoiseAdd", rdram, ctx, runtime);
    }

    void sceSynthesizerReadSample16(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerReadSample16", rdram, ctx, runtime);
    }

    void sceSynthesizerReadSample16Add(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerReadSample16Add", rdram, ctx, runtime);
    }

    void sceSynthesizerReadSample8(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerReadSample8", rdram, ctx, runtime);
    }

    void sceSynthesizerReadSample8Add(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerReadSample8Add", rdram, ctx, runtime);
    }

    void sceSynthesizerResetPart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerResetPart", rdram, ctx, runtime);
    }

    void sceSynthesizerRestorDma(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerRestorDma", rdram, ctx, runtime);
    }

    void sceSynthesizerSelectPatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSelectPatch", rdram, ctx, runtime);
    }

    void sceSynthesizerSendShortMessage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSendShortMessage", rdram, ctx, runtime);
    }

    void sceSynthesizerSetMasterVolume(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetMasterVolume", rdram, ctx, runtime);
    }

    void sceSynthesizerSetRVoice(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetRVoice", rdram, ctx, runtime);
    }

    void sceSynthesizerSetupDma(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetupDma", rdram, ctx, runtime);
    }

    void sceSynthesizerSetupLfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetupLfo", rdram, ctx, runtime);
    }

    void sceSynthesizerSetupMidiModuration(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetupMidiModuration", rdram, ctx, runtime);
    }

    void sceSynthesizerSetupMidiPanpot(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetupMidiPanpot", rdram, ctx, runtime);
    }

    void sceSynthesizerSetupNewNoise(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetupNewNoise", rdram, ctx, runtime);
    }

    void sceSynthesizerSetupReleaseEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetupReleaseEnv", rdram, ctx, runtime);
    }

    void sceSynthesizerSetuptEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetuptEnv", rdram, ctx, runtime);
    }

    void sceSynthesizerSetupTruncateTvaEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetupTruncateTvaEnv", rdram, ctx, runtime);
    }

    void sceSynthesizerSetupTruncateTvfPitchEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerSetupTruncateTvfPitchEnv", rdram, ctx, runtime);
    }

    void sceSynthesizerTonegenerator(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerTonegenerator", rdram, ctx, runtime);
    }

    void sceSynthesizerTransposeMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerTransposeMatrix", rdram, ctx, runtime);
    }

    void sceSynthesizerTvfProcI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerTvfProcI", rdram, ctx, runtime);
    }

    void sceSynthesizerTvfProcNI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerTvfProcNI", rdram, ctx, runtime);
    }

    void sceSynthesizerWaitDmaFromSPR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerWaitDmaFromSPR", rdram, ctx, runtime);
    }

    void sceSynthesizerWaitDmaToSPR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthesizerWaitDmaToSPR", rdram, ctx, runtime);
    }

    void sceSynthsizerGetDrumPatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthsizerGetDrumPatch", rdram, ctx, runtime);
    }

    void sceSynthsizerGetMeloPatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthsizerGetMeloPatch", rdram, ctx, runtime);
    }

    void sceSynthsizerLfoNoise(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthsizerLfoNoise", rdram, ctx, runtime);
    }

    void sceSynthSizerLfoTriangle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSynthSizerLfoTriangle", rdram, ctx, runtime);
    }

    void sceTtyHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceTtyHandler", rdram, ctx, runtime);
    }

    void sceTtyInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceTtyInit", rdram, ctx, runtime);
    }

    void sceTtyRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceTtyRead", rdram, ctx, runtime);
    }

    void sceTtyWrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceTtyWrite", rdram, ctx, runtime);
    }

    void sceVpu0Reset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVpu0Reset", rdram, ctx, runtime);
    }

    void sceVu0AddVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0AddVector", rdram, ctx, runtime);
    }

    void sceVu0ApplyMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ApplyMatrix", rdram, ctx, runtime);
    }

    void sceVu0CameraMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0CameraMatrix", rdram, ctx, runtime);
    }

    void sceVu0ClampVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ClampVector", rdram, ctx, runtime);
    }

    void sceVu0ClipAll(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ClipAll", rdram, ctx, runtime);
    }

    void sceVu0ClipScreen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ClipScreen", rdram, ctx, runtime);
    }

    void sceVu0ClipScreen3(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ClipScreen3", rdram, ctx, runtime);
    }

    void sceVu0CopyMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0CopyMatrix", rdram, ctx, runtime);
    }

    void sceVu0CopyVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0CopyVector", rdram, ctx, runtime);
    }

    void sceVu0CopyVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0CopyVectorXYZ", rdram, ctx, runtime);
    }

    void sceVu0DivVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0DivVector", rdram, ctx, runtime);
    }

    void sceVu0DivVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0DivVectorXYZ", rdram, ctx, runtime);
    }

    void sceVu0DropShadowMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0DropShadowMatrix", rdram, ctx, runtime);
    }

    void sceVu0FTOI0Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0FTOI0Vector", rdram, ctx, runtime);
    }

    void sceVu0FTOI4Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0FTOI4Vector", rdram, ctx, runtime);
    }

    void sceVu0InnerProduct(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0InnerProduct", rdram, ctx, runtime);
    }

    void sceVu0InterVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0InterVector", rdram, ctx, runtime);
    }

    void sceVu0InterVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0InterVectorXYZ", rdram, ctx, runtime);
    }

    void sceVu0InversMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0InversMatrix", rdram, ctx, runtime);
    }

    void sceVu0ITOF0Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ITOF0Vector", rdram, ctx, runtime);
    }

    void sceVu0ITOF12Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ITOF12Vector", rdram, ctx, runtime);
    }

    void sceVu0ITOF4Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ITOF4Vector", rdram, ctx, runtime);
    }

    void sceVu0LightColorMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0LightColorMatrix", rdram, ctx, runtime);
    }

    void sceVu0MulMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0MulMatrix", rdram, ctx, runtime);
    }

    void sceVu0MulVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0MulVector", rdram, ctx, runtime);
    }

    void sceVu0Normalize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0Normalize", rdram, ctx, runtime);
    }

    void sceVu0NormalLightMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0NormalLightMatrix", rdram, ctx, runtime);
    }

    void sceVu0OuterProduct(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0OuterProduct", rdram, ctx, runtime);
    }

    void sceVu0RotMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0RotMatrix", rdram, ctx, runtime);
    }

    void sceVu0RotMatrixX(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0RotMatrixX", rdram, ctx, runtime);
    }

    void sceVu0RotMatrixY(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0RotMatrixY", rdram, ctx, runtime);
    }

    void sceVu0RotMatrixZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0RotMatrixZ", rdram, ctx, runtime);
    }

    void sceVu0RotTransPers(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0RotTransPers", rdram, ctx, runtime);
    }

    void sceVu0RotTransPersN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0RotTransPersN", rdram, ctx, runtime);
    }

    void sceVu0ScaleVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ScaleVector", rdram, ctx, runtime);
    }

    void sceVu0ScaleVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ScaleVectorXYZ", rdram, ctx, runtime);
    }

    void sceVu0SubVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0SubVector", rdram, ctx, runtime);
    }

    void sceVu0TransMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0TransMatrix", rdram, ctx, runtime);
    }

    void sceVu0TransposeMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0TransposeMatrix", rdram, ctx, runtime);
    }

    void sceVu0UnitMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0UnitMatrix", rdram, ctx, runtime);
    }

    void sceVu0ViewScreenMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceVu0ViewScreenMatrix", rdram, ctx, runtime);
    }

    void sceWrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceWrite", rdram, ctx, runtime);
    }

    void srand(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("srand", rdram, ctx, runtime);
    }

    void stat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("stat", rdram, ctx, runtime);
    }

    void strcasecmp(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("strcasecmp", rdram, ctx, runtime);
    }

    void vfprintf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("vfprintf", rdram, ctx, runtime);
    }

    void vsprintf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("vsprintf", rdram, ctx, runtime);
    }

    void write(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("write", rdram, ctx, runtime);
    }

    // END AUTO-GENERATED FALLBACK STUBS

    void TODO(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("unknown", rdram, ctx, runtime);
    }

    void TODO_NAMED(const char *name, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t stub_num = getRegU32(ctx, 2);   // $v0
        uint32_t caller_ra = getRegU32(ctx, 31); // $ra

        std::cerr << "Warning: Unimplemented PS2 stub called. name=" << (name ? name : "unknown")
                  << " PC=0x" << std::hex << ctx->pc
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
