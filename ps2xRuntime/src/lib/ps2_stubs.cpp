#include "ps2_stubs.h"
#include "ps2_runtime.h"
#include "raylib.h"
#include <iostream>
#include <iomanip>
#include <fstream>
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

// Audio Manager for sound playback using extracted WAV files
namespace audio_manager
{
    static std::unordered_map<int, Sound> g_loaded_sounds;
    static bool g_audio_initialized = false;
    static std::string g_sounds_path;
    static std::mutex g_audio_mutex;
    static int g_sounds_loaded = 0;

    void InitializeAudio()
    {
        if (g_audio_initialized) return;

        // Initialize raylib audio device
        InitAudioDevice();
        SetMasterVolume(1.0f);
        g_audio_initialized = true;
        std::cout << "[AUDIO] Audio device initialized" << std::endl;
    }

    void SetSoundsPath(const std::string& path)
    {
        g_sounds_path = path;
        std::cout << "[AUDIO] Sounds path set to: " << path << std::endl;
    }

    // Pre-load available sounds on initialization
    static std::vector<int> g_available_sound_ids;

    void PreloadAvailableSounds()
    {
        if (g_sounds_path.empty()) return;

        // Note: Called without lock, caller must hold lock
        // Scan directory for available sound files
        for (const auto& entry : std::filesystem::directory_iterator(g_sounds_path)) {
            if (entry.path().extension() == ".wav") {
                std::string filename = entry.path().stem().string();
                // Parse "sound_XXX" format
                if (filename.rfind("sound_", 0) == 0) {
                    try {
                        int id = std::stoi(filename.substr(6));
                        g_available_sound_ids.push_back(id);
                    } catch (...) {}
                }
            }
        }

        std::cout << "[AUDIO] Found " << g_available_sound_ids.size() << " sound files" << std::endl;
    }

    // Internal load function - caller must hold the mutex
    static bool LoadSoundByIdInternal(int soundId)
    {
        if (!g_audio_initialized) InitializeAudio();

        // Initialize available sounds list if needed
        static bool sounds_scanned = false;
        if (!sounds_scanned && !g_sounds_path.empty()) {
            PreloadAvailableSounds();
            sounds_scanned = true;
        }

        // Check if already loaded
        if (g_loaded_sounds.count(soundId)) return true;

        // Try exact match first
        std::string filename = g_sounds_path + "/sound_" + std::to_string(soundId) + ".wav";

        if (!std::filesystem::exists(filename)) {
            // Map sound ID to available sound using modulo
            if (!g_available_sound_ids.empty()) {
                int mapped_id = g_available_sound_ids[soundId % g_available_sound_ids.size()];
                filename = g_sounds_path + "/sound_" + std::to_string(mapped_id) + ".wav";

                if (!std::filesystem::exists(filename)) {
                    return false;
                }
            } else {
                return false;
            }
        }

        Sound sound = LoadSound(filename.c_str());
        if (sound.frameCount > 0) {
            g_loaded_sounds[soundId] = sound;
            g_sounds_loaded++;
            if (g_sounds_loaded <= 10) {
                std::cout << "[AUDIO] Loaded sound " << soundId << " from " << filename << std::endl;
            }
            return true;
        }

        return false;
    }

    bool LoadSoundById(int soundId)
    {
        std::lock_guard<std::mutex> lock(g_audio_mutex);
        return LoadSoundByIdInternal(soundId);
    }

    void PlaySoundById(int soundId, float volume, float pan)
    {
        if (!g_audio_initialized) return;

        std::lock_guard<std::mutex> lock(g_audio_mutex);

        auto it = g_loaded_sounds.find(soundId);
        if (it == g_loaded_sounds.end()) {
            // Try to load on-demand (internal version doesn't try to lock)
            if (!LoadSoundByIdInternal(soundId)) return;
            it = g_loaded_sounds.find(soundId);
            if (it == g_loaded_sounds.end()) return;
        }

        // Set volume (PS2 uses 0-1024 range, raylib uses 0.0-1.0)
        SetSoundVolume(it->second, volume);
        SetSoundPan(it->second, pan);
        PlaySound(it->second);
    }

    void StopSoundById(int soundId)
    {
        std::lock_guard<std::mutex> lock(g_audio_mutex);
        auto it = g_loaded_sounds.find(soundId);
        if (it != g_loaded_sounds.end()) {
            StopSound(it->second);
        }
    }

    void Cleanup()
    {
        std::lock_guard<std::mutex> lock(g_audio_mutex);
        for (auto& [id, sound] : g_loaded_sounds) {
            UnloadSound(sound);
        }
        g_loaded_sounds.clear();

        if (g_audio_initialized) {
            CloseAudioDevice();
            g_audio_initialized = false;
        }
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

    void TODO(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
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
    // Helper function to continue execution at return address
    static inline void continueAtRa_stub(R5900Context* ctx) {
        ctx->pc = M128I_U32(ctx->r[31], 0);
    }

    // CD-ROM stubs - bypass loading waits by always returning "complete"
    void sceCdSync_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // sceCdSync(mode) - wait for CD operation to complete
        // Returns: 0 = complete, 1 = busy
        static int call_count = 0;
        { // always print
            std::cout << "sceCdSync_stub: Returning complete (call #" << call_count << ")" << std::endl;
        }
        setReturnS32(ctx, 0);
        continueAtRa_stub(ctx);
    }

    void sceCdSyncS_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int call_count = 0;
        call_count++;

        // Debug: Print $s3 and calculated CD status address
        uint32_t s3 = M128I_U32(ctx->r[19], 0);
        uint32_t cdStatusAddr = s3 + 0xFFFF942Cu; // unsigned to avoid overflow
        uint32_t cdStatus = (cdStatusAddr < 0x02000000) ? *reinterpret_cast<uint32_t*>(rdram + cdStatusAddr) : 0;

        if (call_count <= 10) {
            std::cout << "sceCdSyncS_stub #" << call_count
                      << ": $s3=0x" << std::hex << s3
                      << ", cdAddr=0x" << cdStatusAddr
                      << ", cdStatus=" << std::dec << cdStatus << std::endl;
        }
        setReturnS32(ctx, 0);
        uint32_t ra = M128I_U32(ctx->r[31], 0);
        if (call_count <= 10) {
            std::cout << "  sceCdSyncS returning to PC=0x" << std::hex << ra << std::dec << std::endl;
        }
        continueAtRa_stub(ctx);
    }

    void sceCdGetError_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
        continueAtRa_stub(ctx);
    }

    // Global ISO file handle for CD-ROM emulation
    static FILE* g_iso_file = nullptr;
    static bool g_iso_initialized = false;
    static const size_t SECTOR_SIZE = 2048;  // PS2 CD-ROM sector size

    static bool initISOFile() {
        if (g_iso_initialized) return g_iso_file != nullptr;
        g_iso_initialized = true;

        // Try to find the ISO file in common locations
        std::vector<std::string> iso_paths = {
            "Sly Cooper and the Thievius Raccoonus (USA).iso",
            "../Sly Cooper and the Thievius Raccoonus (USA).iso",
            "../../Sly Cooper and the Thievius Raccoonus (USA).iso",
            "../../../Sly Cooper and the Thievius Raccoonus (USA)/Sly Cooper and the Thievius Raccoonus (USA).iso",
            "../../../../Sly Cooper and the Thievius Raccoonus (USA)/Sly Cooper and the Thievius Raccoonus (USA).iso"
        };

        for (const auto& path : iso_paths) {
            if (std::filesystem::exists(path)) {
                g_iso_file = ::fopen(path.c_str(), "rb");
                if (g_iso_file) {
                    std::cout << "[CD-ROM] ISO file opened: " << path << std::endl;
                    return true;
                }
            }
        }

        std::cerr << "[CD-ROM] WARNING: Could not find ISO file!" << std::endl;
        std::cerr << "[CD-ROM] Tried: " << std::endl;
        for (const auto& path : iso_paths) {
            std::cerr << "  - " << path << std::endl;
        }
        return false;
    }

    void sceCdRead_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t lsn = getRegU32(ctx, 4);
        uint32_t sectors = getRegU32(ctx, 5);
        uint32_t buffer = getRegU32(ctx, 6);

        static int read_count = 0;
        read_count++;

        if (read_count <= 20) {
            std::cout << "[CD-ROM] sceCdRead #" << read_count
                      << ": lsn=" << lsn << " sectors=" << sectors
                      << " buffer=0x" << std::hex << buffer << std::dec << std::endl;
        }

        // Initialize ISO if not done yet
        if (!g_iso_initialized) {
            initISOFile();
        }

        if (buffer > 0 && buffer < 0x02000000 && sectors > 0) {
            uint8_t* dest = rdram + (buffer & 0x1FFFFFF);
            size_t bytes_to_read = sectors * SECTOR_SIZE;

            if (g_iso_file) {
                // Seek to the correct position in ISO
                // ISO files typically have sectors at offset = lsn * SECTOR_SIZE
                off_t offset = (off_t)lsn * SECTOR_SIZE;
                if (::fseek(g_iso_file, offset, SEEK_SET) == 0) {
                    size_t bytes_read = ::fread(dest, 1, bytes_to_read, g_iso_file);
                    if (bytes_read != bytes_to_read) {
                        if (read_count <= 20) {
                            std::cerr << "[CD-ROM] Warning: Read only " << bytes_read
                                      << " of " << bytes_to_read << " bytes" << std::endl;
                        }
                        // Zero the rest
                        std::memset(dest + bytes_read, 0, bytes_to_read - bytes_read);
                    }
                } else {
                    if (read_count <= 20) {
                        std::cerr << "[CD-ROM] Error: Seek failed to lsn " << lsn << std::endl;
                    }
                    std::memset(dest, 0, bytes_to_read);
                }
            } else {
                // No ISO file - zero the buffer (fallback)
                std::memset(dest, 0, bytes_to_read);
            }
        }

        setReturnS32(ctx, 1);  // Return success
        continueAtRa_stub(ctx);
    }


    // Debug wrapper for CD init check at 0x203880
    // This replaces the recompiled entry_203880 to debug the CD status check
    void debug_cd_check_203880(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int call_count = 0;
        call_count++;

        // Replicate the logic of entry_203880:
        // 0x203880: lw $v1, 0xFFFF942C($s3)  -- load CD status
        // 0x203884: li $v0, 6
        // if ($v1 != $v0) goto return at 0x2039F4

        uint32_t s3 = M128I_U32(ctx->r[19], 0);
        uint32_t addr = s3 + 0xFFFF942Cu;
        uint32_t cdStatus = (addr < 0x02000000) ? *reinterpret_cast<uint32_t*>(rdram + addr) : 0;

        if (call_count <= 10 || call_count % 100000 == 0) {
            std::cout << "debug_cd_check #" << call_count
                      << ": $s3=0x" << std::hex << s3
                      << ", addr=0x" << addr
                      << ", cdStatus=" << std::dec << cdStatus << std::endl;
        }

        // Force cdStatus to 6 so we can proceed
        if (addr < 0x02000000) {
            *reinterpret_cast<uint32_t*>(rdram + addr) = 6;
            cdStatus = 6;
        }

        // Set registers as the original code does
        ctx->r[3] = _mm_set_epi32(0, 0, 0, cdStatus);  // $v1 = cdStatus
        ctx->r[2] = _mm_set_epi32(0, 0, 0, 6);         // $v0 = 6

        if (cdStatus != 6) {
            // Branch to return at 0x2039F4
            ctx->pc = 0x2039F4;
            return;
        }

        // cdStatus == 6, continue to sceCdSyncS at 0x20388c
        // Set RA to 0x203894 (next instruction after jal)
        ctx->r[31] = _mm_set_epi32(0, 0, 0, 0x203894);
        ctx->r[4] = _mm_set_epi32(0, 0, 0, 1);  // $a0 = 1 for sceCdSyncS

        // Set PC to continue at entry_203894 after sceCdSyncS returns
        // sceCdSyncS_stub sets ctx->pc to RA internally
        sceCdSyncS_stub(rdram, ctx, runtime);
    }

    // Debug stub for entry_203894 - check $v0 and either proceed to sceSifInitRpc or loop back
    void debug_entry_203894(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int call_count = 0;
        call_count++;

        uint32_t v0 = M128I_U32(ctx->r[2], 0);

        if (call_count <= 20 || call_count % 50000 == 0) {
            std::cout << "debug_entry_203894 #" << call_count
                      << ": $v0=" << v0
                      << " (need 0 to proceed to sceSifInitRpc)" << std::endl;
        }

        // The original code: if ($v0 != 0) goto 0x2039AC (SignalSema loop)
        // We want to force $v0 = 0 to proceed to sceSifInitRpc
        if (v0 != 0) {
            if (call_count <= 20) {
                std::cout << "  -> Forcing $v0 = 0 to break out of CD init loop!" << std::endl;
            }
            ctx->r[2] = _mm_set_epi32(0, 0, 0, 0);  // Force $v0 = 0
        }

        // Now proceed to sceSifInitRpc at 0x1f7be8
        // Original code at 0x203894: bne $v0, $zero, 0x2039AC
        // Delay slot sets $a0 = $zero + $zero
        ctx->r[4] = _mm_set_epi32(0, 0, 0, 0);  // $a0 = 0

        // Set RA for return from sceSifInitRpc
        ctx->r[31] = _mm_set_epi32(0, 0, 0, 0x2038a4);

        std::cout << "  -> Calling sceSifInitRpc at 0x1f7be8!" << std::endl;

        // Look up and call sceSifInitRpc directly via runtime function table
        auto func = runtime->lookupFunction(0x1f7be8);
        if (func) {
            func(rdram, ctx, runtime);
        } else {
            std::cerr << "ERROR: Could not find sceSifInitRpc at 0x1f7be8!" << std::endl;
            ctx->pc = 0x2038a4;  // Continue anyway
        }
    }

    // Stub for sceCdDiskReady - bypasses CD initialization entirely
    void sceCdDiskReady_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int call_count = 0;
        call_count++;

        uint32_t ra = M128I_U32(ctx->r[31], 0);
        
        if (call_count <= 10) {
            std::cout << "sceCdDiskReady_stub #" << call_count 
                      << ": Returning ready! RA=0x" << std::hex << ra << std::dec << std::endl;
        }

        // Set return value to indicate CD is ready
        // $v0 should be 2 (SCECdComplete) based on PS2 SDK
        setReturnS32(ctx, 2);  // Return 2 = SCECdComplete

        // Patch memory flags to indicate CD is ready
        *reinterpret_cast<int32_t*>(rdram + 0x279410) = 0;   // Status >= 0 = ready
        *reinterpret_cast<int32_t*>(rdram + 0x279444) = 0;   // Status >= 0 = ready
        *reinterpret_cast<uint32_t*>(rdram + 0x27942C) = 6;  // CD status = ready

        // Return directly to caller (don't go through the CD init loop)
        ctx->pc = ra;
    }
    // Stub for sceSifInitRpc - initializes SIF/RPC subsystem
    void sceSifInitRpc_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int call_count = 0;
        call_count++;

        uint32_t s1 = M128I_U32(ctx->r[17], 0);  // $s1 register
        uint32_t statusAddr = (s1 + 0xFFFF9444u) & 0x1FFFFFF;
        int32_t statusBefore = *reinterpret_cast<int32_t*>(rdram + statusAddr);

        if (call_count <= 10) {
            std::cout << "sceSifInitRpc_stub #" << call_count 
                      << ": $s1=0x" << std::hex << s1
                      << ", statusAddr=0x" << statusAddr
                      << ", statusBefore=" << std::dec << statusBefore << std::endl;
        }

        // Set the RPC initialized flag at 0x277134
        *reinterpret_cast<uint32_t*>(rdram + 0x277134) = 1;

        // Set the CD status flag at the dynamic address (based on $s1)
        *reinterpret_cast<int32_t*>(rdram + statusAddr) = 0;  // 0 = ready

        // Also set at fixed address just in case
        *reinterpret_cast<int32_t*>(rdram + 0x279444) = 0;

        // Set return value
        setReturnS32(ctx, 0);

        // Return to caller
        uint32_t ra = M128I_U32(ctx->r[31], 0);
        if (call_count <= 10) {
            std::cout << "  sceSifInitRpc returning to 0x" << std::hex << ra << std::dec << std::endl;
        }
        ctx->pc = ra;
    }
    // Stub for entry_2038a4 - bypasses CD init loop check
    void debug_entry_2038a4(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int call_count = 0;
        call_count++;

        // Get $s1 value for status check
        uint32_t s1 = M128I_U32(ctx->r[17], 0);
        uint32_t statusAddr = (s1 + 0xFFFF9444u) & 0x1FFFFFF;
        
        if (call_count <= 10) {
            std::cout << "debug_entry_2038a4 #" << call_count 
                      << ": $s1=0x" << std::hex << s1
                      << ", statusAddr=0x" << statusAddr << std::dec << std::endl;
        }

        // Force status to be non-negative (>= 0) to exit the loop
        *reinterpret_cast<int32_t*>(rdram + statusAddr) = 0;
        *reinterpret_cast<int32_t*>(rdram + 0x279444) = 0;

        // Set $v0 to 0 (non-negative) so branch to 0x20395C is taken
        ctx->r[2] = _mm_set_epi32(0, 0, 0, 0);

        // Jump to 0x20395C (the exit path)
        if (call_count <= 10) {
            std::cout << "  -> Jumping to 0x20395C to exit CD init" << std::endl;
        }
        ctx->pc = 0x20395C;
    }
    // Stub for entry_203874 - the main PollSema loop entry in CD init
    // This is the inner loop that keeps polling
    void debug_entry_203874(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int call_count = 0;
        call_count++;

        if (call_count <= 10) {
            std::cout << "debug_entry_203874 #" << call_count 
                      << ": Skipping PollSema loop, jumping to exit" << std::endl;
        }

        // Patch CD status to ready
        *reinterpret_cast<uint32_t*>(rdram + 0x27942C) = 6;

        // Instead of entering the PollSema loop, jump directly to the exit
        // The exit of sceCdDiskReady is at 0x2039F4 (epilog)
        ctx->pc = 0x2039F4;
    }

    // ============================================================
    // SOUND SYSTEM STUBS - Intercept and log sound commands
    // ============================================================

    // Stub for sceSifCheckStatRpc - check if RPC call is complete
    // This is what's causing the sound init loop
    void sceSifCheckStatRpc_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int call_count = 0;
        call_count++;

        // $a0 = pointer to RPC client data
        uint32_t clientPtr = getRegU32(ctx, 4);

        if (call_count <= 20) {
            std::cout << "[SOUND] sceSifCheckStatRpc #" << call_count
                      << " client=0x" << std::hex << clientPtr << std::dec << std::endl;
        }

        // Return 0 = RPC call complete (not busy)
        // This tells the game the IOP has finished processing
        setReturnS32(ctx, 0);
        // Force clear g_mpeg.oid_1 to escape video loop
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;
        ctx->pc = getRegU32(ctx, 31);  // Return to caller
    }

    // Stub for snd_SendIOPCommandAndWait - sends command to IOP sound driver
    // This is the main entry point for all sound commands
    void snd_SendIOPCommandAndWait_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int call_count = 0;
        static int sounds_played = 0;
        call_count++;

        // Arguments: $a0 = command, $a1 = param (size), $a2 = data pointer
        uint32_t cmd = getRegU32(ctx, 4);
        uint32_t param = getRegU32(ctx, 5);
        uint32_t dataPtr = getRegU32(ctx, 6);

        // Get data pointer
        uint32_t* data = nullptr;
        if (dataPtr != 0 && dataPtr < 0x2000000) {
            data = (uint32_t*)(rdram + (dataPtr & 0x1FFFFFF));
        }

        // Interpret commands based on 989snd.c from decomp
        const char* cmdName = "UNKNOWN";
        switch (cmd) {
            case 0x00: cmdName = "snd_SetMasterVolume"; break;
            case 0x06: cmdName = "snd_SetPlaybackMode"; break;
            case 0x08: cmdName = "snd_StopAllSounds"; break;
            case 0x09: cmdName = "snd_SetMixerMode"; break;
            case 0x0A: cmdName = "snd_GetFreeSPUDMA"; break;
            case 0x0B: cmdName = "snd_FreeSPUDMA"; break;
            case 0x0D: cmdName = "snd_SetReverbMode"; break;
            case 0x10: cmdName = "snd_PlaySound"; break;
            case 0x11: cmdName = "snd_PlaySoundVolPanPMPB"; break;
            case 0x13: cmdName = "snd_PauseSound"; break;
            case 0x14: cmdName = "snd_ContinueSound"; break;
            case 0x15: cmdName = "snd_StopSound"; break;
            case 0x16: cmdName = "snd_SetSoundVolume"; break;
            case 0x17: cmdName = "snd_SetSoundPan"; break;
            case 0x19: cmdName = "snd_IsSoundPlaying"; break;
            case 0x1A: cmdName = "snd_SetSoundPitch"; break;
            case 0x1B: cmdName = "snd_SetSoundParams"; break;
            case 0x4E: cmdName = "snd_SetGroupVoiceRange"; break;
            default: break;
        }

        // Track command counts for summary
        static std::unordered_map<uint32_t, int> cmd_counts;
        cmd_counts[cmd]++;

        // Log and play sound commands
        if (cmd == 0x11 && data) {  // snd_PlaySoundVolPanPMPB
            sounds_played++;
            uint32_t bank = data[0];
            uint32_t soundId = data[1];
            uint32_t vol = data[2];
            uint32_t pan = data[3];
            uint32_t pitchMod = (param >= 20) ? data[4] : 0;
            uint32_t pitchBend = (param >= 24) ? data[5] : 0;

            // Log (limit spam)
            if (sounds_played <= 20 || sounds_played % 100 == 0) {
                std::cout << "[SOUND] PlaySound #" << sounds_played
                          << ": id=" << soundId
                          << " vol=" << vol
                          << " pan=" << (int32_t)pan << std::endl;
            }

            // PLAY ACTUAL AUDIO using raylib
            float volume = std::min(1.0f, vol / 1024.0f);  // PS2 uses 0-1024
            float panf = (pan / 32768.0f);  // PS2 pan: -32768 to 32768, raylib: 0.0 to 1.0
            panf = (panf + 1.0f) / 2.0f;    // Convert to 0-1 range
            audio_manager::PlaySoundById(soundId, volume, panf);
        }
        else if (cmd == 0x10 && data) {  // snd_PlaySound (simpler)
            sounds_played++;
            uint32_t soundId = data[1];

            if (sounds_played <= 20) {
                std::cout << "[SOUND] PlaySound(simple) #" << sounds_played
                          << ": id=" << soundId << std::endl;
            }

            audio_manager::PlaySoundById(soundId, 1.0f, 0.5f);
        }
        else if (cmd == 0x15 && data) {  // snd_StopSound
            std::cout << "[SOUND] StopSound: handle=0x" << std::hex << data[0] << std::dec << std::endl;
        }
        else if (cmd == 0x00 && data) {  // snd_SetMasterVolume
            std::cout << "[SOUND] SetMasterVolume: left=" << data[0] << " right=" << data[1] << std::endl;
        }
        else if (cmd == 0x19 && data) {  // snd_IsSoundPlaying
            std::cout << "[SOUND] IsSoundPlaying: handle=0x" << std::hex << data[0] << std::dec << std::endl;
        }
        else if (cmd == 0x16 && data) {  // snd_SetSoundVolume
            std::cout << "[SOUND] SetVolume: handle=0x" << std::hex << data[0]
                      << std::dec << " vol=" << data[1] << std::endl;
        }
        else if (cmd == 0x17 && data) {  // snd_SetSoundPan
            std::cout << "[SOUND] SetPan: handle=0x" << std::hex << data[0]
                      << std::dec << " pan=" << (int32_t)data[1] << std::endl;
        }
        else if (cmd == 0x1A && data) {  // snd_SetSoundPitch - suppress spam
            if (cmd_counts[cmd] <= 3) {
                std::cout << "[SOUND] SetPitch: handle=0x" << std::hex << data[0]
                          << std::dec << " pitch=" << data[1]
                          << " (suppressing further)" << std::endl;
            }
        }
        else if (cmd_counts[cmd] <= 5) {  // Log first 5 of each command type
            std::cout << "[SOUND] cmd=0x" << std::hex << cmd << std::dec
                      << " (" << cmdName << ") param=" << param;
            if (data && param >= 4) {
                std::cout << " data[0]=0x" << std::hex << data[0];
                if (param >= 8) std::cout << " [1]=0x" << data[1];
                if (param >= 12) std::cout << " [2]=0x" << data[2];
                std::cout << std::dec;
            }
            std::cout << std::endl;
        }

        // Print summary every 500 calls
        if (call_count % 500 == 0) {
            std::cout << "\n[SOUND] === Summary (" << call_count << " calls, " << sounds_played << " sounds) ===" << std::endl;
            for (auto& [c, count] : cmd_counts) {
                const char* name = "?";
                switch(c) {
                    case 0x00: name = "MasterVol"; break;
                    case 0x10: name = "Play"; break;
                    case 0x11: name = "PlayVPPMPB"; break;
                    case 0x15: name = "Stop"; break;
                    case 0x16: name = "SetVol"; break;
                    case 0x17: name = "SetPan"; break;
                    case 0x19: name = "IsPlaying"; break;
                    case 0x1A: name = "SetPitch"; break;
                    default: name = cmdName; break;
                }
                std::cout << "[SOUND]   0x" << std::hex << c << std::dec
                          << " " << name << ": " << count << "x" << std::endl;
            }
            std::cout << std::endl;
        }

        // Return 0 (success) or fake handle for PlaySound
        if (cmd == 0x11) {
            // Return a fake sound handle
            setReturnS32(ctx, 0x1000 + sounds_played);
        } else if (cmd == 0x19) {
            // IsSoundPlaying - return 0 (not playing)
            setReturnS32(ctx, 0);
        } else {
            setReturnS32(ctx, 0);
        }
        // Force clear g_mpeg.oid_1 to escape video loop
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;
        ctx->pc = getRegU32(ctx, 31);
    }

    // Stub for snd_GotReturns - check for IOP responses
    void snd_GotReturns_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int call_count = 0;
        call_count++;

        if (call_count <= 20) {
            std::cout << "[SOUND] snd_GotReturns #" << call_count << std::endl;
        }

        // Just return - no responses pending
        setReturnS32(ctx, 0);
        // Force clear g_mpeg.oid_1 to escape video loop
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;
        ctx->pc = getRegU32(ctx, 31);
    }

    // Stub for sceSifCallRpc - handles RPC calls to IOP
    // This is used for sound bank loading among other things
    void sceSifCallRpc_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int call_count = 0;
        call_count++;

        // Arguments from MIPS calling convention:
        // $a0 = client data pointer
        // $a1 = function number
        // $a2 = mode (0=blocking, 1=non-blocking)
        // $a3 = send buffer
        // stack: send size, recv buffer, recv size, callback, callback data
        uint32_t clientPtr = getRegU32(ctx, 4);
        uint32_t funcNum = getRegU32(ctx, 5);
        uint32_t mode = getRegU32(ctx, 6);
        uint32_t sendBuf = getRegU32(ctx, 7);

        // Read stack arguments
        uint32_t sp = getRegU32(ctx, 29);
        uint32_t sendSize = *(uint32_t*)(rdram + ((sp + 0x10) & 0x1FFFFFF));
        uint32_t recvBuf = *(uint32_t*)(rdram + ((sp + 0x14) & 0x1FFFFFF));
        uint32_t recvSize = *(uint32_t*)(rdram + ((sp + 0x18) & 0x1FFFFFF));

        if (call_count <= 30) {
            std::cout << "[SOUND] sceSifCallRpc #" << call_count
                      << " func=" << funcNum
                      << " mode=" << mode
                      << " sendBuf=0x" << std::hex << sendBuf
                      << " recvBuf=0x" << recvBuf << std::dec << std::endl;
        }

        // Check if this is a bank load RPC (function 3 to loader service)
        // gLoadReturnValue is at 0x260e00
        const uint32_t LOAD_RETURN_VALUE_ADDR = 0x260e00;

        if (funcNum == 3 && recvBuf != 0) {
            // This is snd_BankLoadByLoc - return a fake bank pointer
            // The game expects a valid pointer, we'll give it a fake one
            // in an unused memory area
            static uint32_t fakeBankPtr = 0x1F00000;  // High memory, hopefully unused

            // Write the fake bank pointer to the receive buffer
            uint32_t* recvPtr = (uint32_t*)(rdram + (recvBuf & 0x1FFFFFF));
            *recvPtr = fakeBankPtr;

            // Also write to gLoadReturnValue directly in case it's checked there
            uint32_t* loadRetVal = (uint32_t*)(rdram + (LOAD_RETURN_VALUE_ADDR & 0x1FFFFFF));
            *loadRetVal = fakeBankPtr;

            std::cout << "[SOUND] Bank load RPC - returning fake bank ptr 0x"
                      << std::hex << fakeBankPtr << std::dec << std::endl;

            fakeBankPtr += 0x10000;  // Next bank gets different address
        }

        // Return 0 (success)
        setReturnS32(ctx, 0);
        // Force clear g_mpeg.oid_1 to escape video loop
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;
        ctx->pc = getRegU32(ctx, 31);
    }

    // ============================================================
    // CONTROLLER STUBS - Map raylib keyboard/gamepad to PS2 pad
    // ============================================================

    // PS2 pad button masks (from libpad documentation)
    // These are the bits in the button data word
    #define PAD_SELECT   0x0001
    #define PAD_L3       0x0002
    #define PAD_R3       0x0004
    #define PAD_START    0x0008
    #define PAD_UP       0x0010
    #define PAD_RIGHT    0x0020
    #define PAD_DOWN     0x0040
    #define PAD_LEFT     0x0080
    #define PAD_L2       0x0100
    #define PAD_R2       0x0200
    #define PAD_L1       0x0400
    #define PAD_R1       0x0800
    #define PAD_TRIANGLE 0x1000
    #define PAD_CIRCLE   0x2000
    #define PAD_CROSS    0x4000
    #define PAD_SQUARE   0x8000

    // scePadInit - Initialize pad library
    void scePadInit_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int call_count = 0;
        { // always print
            std::cout << "[PAD] *** scePadInit STUB CALLED ***" << std::endl;
        }
        setReturnS32(ctx, 1);  // Return success
        // Force clear g_mpeg.oid_1 to escape video loop
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;
        ctx->pc = getRegU32(ctx, 31);
    }

    // scePadPortOpen - Open a controller port
    void scePadPortOpen_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t port = getRegU32(ctx, 4);
        uint32_t slot = getRegU32(ctx, 5);
        uint32_t dmaBuffer = getRegU32(ctx, 6);

        static int call_count = 0;
        { // always print
            std::cout << "[PAD] scePadPortOpen port=" << port << " slot=" << slot
                      << " dma=0x" << std::hex << dmaBuffer << std::dec << std::endl;
        }

        setReturnS32(ctx, 1);  // Return success (non-zero = success)
        // Force clear g_mpeg.oid_1 to escape video loop
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;
        ctx->pc = getRegU32(ctx, 31);
    }

    // scePadGetState - Get controller state
    // Returns: 0=disconnected, 1=finding, 2=ready/stable, 6=ready
    void scePadGetState_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int call_count = 0;
        call_count++;

        if (call_count <= 5) {
            std::cout << "[PAD] scePadGetState called" << std::endl;
        }

        // Return 6 = stable/ready (same as sceCdComplete for CD)
        setReturnS32(ctx, 6);
        // Force clear g_mpeg.oid_1 to escape video loop
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;
        ctx->pc = getRegU32(ctx, 31);
    }

    // scePadRead - Read controller data
    // The data buffer format:
    // [0] = status byte
    // [1] = data length / 2
    // [2-3] = button data (16 bits, active low)
    // [4] = right stick X (0-255, 128=center)
    // [5] = right stick Y
    // [6] = left stick X
    // [7] = left stick Y
    void scePadRead_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t port = getRegU32(ctx, 4);
        uint32_t slot = getRegU32(ctx, 5);
        uint32_t dataAddr = getRegU32(ctx, 6);

        static int call_count = 0;
        call_count++;

        if (dataAddr == 0 || dataAddr > 0x2000000) {
            setReturnS32(ctx, 0);
        // Force clear g_mpeg.oid_1 to escape video loop
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;
            ctx->pc = getRegU32(ctx, 31);
            return;
        }

        uint8_t* data = rdram + (dataAddr & 0x1FFFFFF);

        // Read keyboard/gamepad input from raylib
        uint16_t buttons = 0xFFFF;  // All buttons released (active low!)

        // Map keyboard to PS2 buttons
        if (IsKeyDown(KEY_ENTER) || IsKeyDown(KEY_SPACE))  buttons &= ~PAD_CROSS;    // X button
        if (IsKeyDown(KEY_BACKSPACE) || IsKeyDown(KEY_ESCAPE)) buttons &= ~PAD_CIRCLE;   // Circle
        if (IsKeyDown(KEY_Q))         buttons &= ~PAD_SQUARE;   // Square
        if (IsKeyDown(KEY_E))         buttons &= ~PAD_TRIANGLE; // Triangle
        if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))    buttons &= ~PAD_UP;
        if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))  buttons &= ~PAD_DOWN;
        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))  buttons &= ~PAD_LEFT;
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) buttons &= ~PAD_RIGHT;
        if (IsKeyDown(KEY_TAB))       buttons &= ~PAD_SELECT;
        if (IsKeyDown(KEY_ENTER))     buttons &= ~PAD_START;
        if (IsKeyDown(KEY_Z))         buttons &= ~PAD_L1;
        if (IsKeyDown(KEY_C))         buttons &= ~PAD_R1;
        if (IsKeyDown(KEY_ONE))       buttons &= ~PAD_L2;
        if (IsKeyDown(KEY_THREE))     buttons &= ~PAD_R2;

        // Also check gamepad if connected
        if (IsGamepadAvailable(0)) {
            if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))  buttons &= ~PAD_CROSS;
            if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) buttons &= ~PAD_CIRCLE;
            if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT))  buttons &= ~PAD_SQUARE;
            if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_UP))    buttons &= ~PAD_TRIANGLE;
            if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP))     buttons &= ~PAD_UP;
            if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN))   buttons &= ~PAD_DOWN;
            if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT))   buttons &= ~PAD_LEFT;
            if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT))  buttons &= ~PAD_RIGHT;
            if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_LEFT))      buttons &= ~PAD_SELECT;
            if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_RIGHT))     buttons &= ~PAD_START;
            if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1))   buttons &= ~PAD_L1;
            if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1))  buttons &= ~PAD_R1;
            if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_TRIGGER_2))   buttons &= ~PAD_L2;
            if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_2))  buttons &= ~PAD_R2;
        }

        // Fill the data buffer
        data[0] = 0x00;  // Status: success
        data[1] = 0x08;  // Data length: 16 bytes (8 halfwords) / 2 = 8? Actually means mode
        // Button data (little endian, active low)
        data[2] = buttons & 0xFF;
        data[3] = (buttons >> 8) & 0xFF;
        // Analog sticks (center = 128)
        data[4] = 128;  // Right stick X
        data[5] = 128;  // Right stick Y
        data[6] = 128;  // Left stick X
        data[7] = 128;  // Left stick Y

        // Read analog sticks from gamepad if available
        if (IsGamepadAvailable(0)) {
            float lx = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
            float ly = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
            float rx = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X);
            float ry = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y);

            data[6] = (uint8_t)(128 + lx * 127);  // Left stick X
            data[7] = (uint8_t)(128 + ly * 127);  // Left stick Y
            data[4] = (uint8_t)(128 + rx * 127);  // Right stick X
            data[5] = (uint8_t)(128 + ry * 127);  // Right stick Y
        }

        // Log button presses (but not too often)
        if (call_count <= 10 || (buttons != 0xFFFF && call_count % 60 == 0)) {
            std::cout << "[PAD] scePadRead: buttons=0x" << std::hex << buttons << std::dec << std::endl;
        }

        setReturnS32(ctx, 1);  // Return success
        // Force clear g_mpeg.oid_1 to escape video loop
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;
        ctx->pc = getRegU32(ctx, 31);
    }

    // scePadInfoMode - Get pad mode info
    void scePadInfoMode_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t port = getRegU32(ctx, 4);
        uint32_t slot = getRegU32(ctx, 5);
        uint32_t term = getRegU32(ctx, 6);
        uint32_t offs = getRegU32(ctx, 7);

        static int call_count = 0;
        { // always print
            std::cout << "[PAD] scePadInfoMode port=" << port << " slot=" << slot
                      << " term=" << term << " offs=" << offs << std::endl;
        }

        // Return 1 for DualShock2 mode support
        setReturnS32(ctx, 1);
        // Force clear g_mpeg.oid_1 to escape video loop
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;
        ctx->pc = getRegU32(ctx, 31);
    }

    // scePadSetMainMode - Set controller mode
    void scePadSetMainMode_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int call_count = 0;
        { // always print
            std::cout << "[PAD] scePadSetMainMode called" << std::endl;
        }
        setReturnS32(ctx, 1);
        // Force clear g_mpeg.oid_1 to escape video loop
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;
        ctx->pc = getRegU32(ctx, 31);
    }

    // scePadInfoAct - Get actuator (vibration) info
    void scePadInfoAct_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int call_count = 0;
        { // always print
            std::cout << "[PAD] scePadInfoAct called" << std::endl;
        }
        setReturnS32(ctx, 1);  // Has actuators
        // Force clear g_mpeg.oid_1 to escape video loop
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;
        ctx->pc = getRegU32(ctx, 31);
    }

    // scePadSetActAlign - Set actuator alignment
    void scePadSetActAlign_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int call_count = 0;
        { // always print
            std::cout << "[PAD] scePadSetActAlign called" << std::endl;
        }
        setReturnS32(ctx, 1);
        // Force clear g_mpeg.oid_1 to escape video loop
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;
        ctx->pc = getRegU32(ctx, 31);
    }

    // scePadSetActDirect - Direct actuator control (vibration)
    void scePadSetActDirect_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // Silently ignore vibration commands
        setReturnS32(ctx, 1);
        // Force clear g_mpeg.oid_1 to escape video loop
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;
        ctx->pc = getRegU32(ctx, 31);
    }

    // scePadInfoPressMode - Check pressure sensitivity support
    void scePadInfoPressMode_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);  // Supports pressure
        // Force clear g_mpeg.oid_1 to escape video loop
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;
        ctx->pc = getRegU32(ctx, 31);
    }

    // scePadEnterPressMode - Enter pressure mode
    void scePadEnterPressMode_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
        // Force clear g_mpeg.oid_1 to escape video loop
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;
        ctx->pc = getRegU32(ctx, 31);
    }

}

namespace ps2_stubs {
    // FlushFrames stub - Skip frame waiting to allow game to progress
    void FlushFrames_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int call_count = 0;
        if (++call_count <= 5) {
            std::cout << "[STUB] FlushFrames called, clearing g_mpeg" << std::endl;
        }
        // Force clear g_mpeg.oid_1 to escape video loop
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;
        ctx->pc = getRegU32(ctx, 31);
    }
}

// Debug: Check g_mpeg value at runtime
namespace ps2_stubs {
    void DebugCheckMpeg_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // g_mpeg.oid_1 is at 0x269A00 (relative to rdram: 0x169A00)
        uint32_t mpeg_oid1 = *reinterpret_cast<uint32_t*>(rdram + 0x169A00);
        static int check_count = 0;
        if (++check_count <= 10) {
            std::cout << "[DEBUG] g_mpeg.oid_1 at 0x269A00 = " << mpeg_oid1 << std::endl;
        }
        // Force clear g_mpeg.oid_1 to escape video loop
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;
        ctx->pc = getRegU32(ctx, 31);
    }
}

namespace ps2_stubs {
    // WaitSema function stub - clear g_mpeg to escape video loop
    void WaitSema_func_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int call_count = 0;
        call_count++;

        // Clear g_mpeg.oid_1 to escape video loop
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;

        if (call_count <= 3) {
            std::cout << "[WAITSEMA STUB] Clearing g_mpeg, call #" << call_count << std::endl;
        }

        // Just return - don't actually wait
        ctx->pc = getRegU32(ctx, 31);
    }
}

namespace ps2_stubs {
    // ExecuteOids stub - clear OIDs and return without executing MPEG
    // This allows the game to skip video playback
    void ExecuteOids_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // $a0 (register 4) contains 'this' pointer to CMpeg object
        uint32_t mpegAddr = getRegU32(ctx, 4);
        
        static int call_count = 0;
        if (++call_count <= 5) {
            std::cout << "[MPEG] ExecuteOids called on CMpeg at 0x" << std::hex << mpegAddr << std::dec << std::endl;
        }
        
        // Clear oid_1 and oid_2 in the CMpeg struct (offset 0 and 4)
        if (mpegAddr >= 0x100000 && mpegAddr < 0x2000000) {
            uint32_t offset = mpegAddr - 0x100000;
            *reinterpret_cast<uint32_t*>(rdram + offset) = 0;      // oid_1
            *reinterpret_cast<uint32_t*>(rdram + offset + 4) = 0;  // oid_2
            
            if (call_count <= 5) {
                std::cout << "[MPEG] Cleared oid_1 and oid_2" << std::endl;
            }
        }
        
        // Return without executing any video
        ctx->pc = getRegU32(ctx, 31);
    }
}

namespace ps2_stubs {
    // sceMpegIsEnd stub - CRITICAL for MPEG video skipping
    // This PS2 SDK function checks if MPEG video is done playing
    // Address: 0x20B0B0 (2142384)
    // By returning 1 (true), we tell the game the video is finished,
    // which breaks the Execute loop in CMpeg::Execute
    void sceMpegIsEnd_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int call_count = 0;
        if (++call_count <= 10) {
            std::cout << "[MPEG] sceMpegIsEnd called, returning 1 (video done)" << std::endl;
        }

        // Return 1 in $v0 - video is finished
        setReturnS32(ctx, 1);

        // Return to caller
        ctx->pc = getRegU32(ctx, 31);
    }
}

namespace ps2_stubs {
    // ==== DEBUG VERSIONS - Log but let real code run ====

    // Open log file for MPEG data dumps
    static std::ofstream mpeg_log_file;
    static bool mpeg_log_initialized = false;

    static void initMpegLog() {
        if (!mpeg_log_initialized) {
            mpeg_log_file.open("mpeg_trace.log", std::ios::out | std::ios::trunc);
            if (mpeg_log_file.is_open()) {
                mpeg_log_file << "=== MPEG Debug Trace ===" << std::endl;
                mpeg_log_file << "Logging all MPEG operations without skipping" << std::endl;
                mpeg_log_file << std::endl;
            }
            mpeg_log_initialized = true;
        }
    }

    // Dump CMpeg structure (based on decomp analysis)
    // CMpeg structure layout (from sly1-decomp):
    // +0x00: OID* oid_1        - Current operation ID 1
    // +0x04: OID* oid_2        - Current operation ID 2
    // +0x08: void* pvBuffer    - MPEG buffer pointer
    // +0x0C: int cbBuffer      - Buffer size
    // +0x10: sceMpeg mpeg      - PS2 SDK MPEG handle
    // +0x14+: More fields...
    static void dumpCMpegStruct(uint8_t* rdram, uint32_t mpegAddr, std::ostream& out) {
        if (mpegAddr < 0x100000 || mpegAddr >= 0x2000000) {
            out << "  [Invalid CMpeg address]" << std::endl;
            return;
        }

        uint32_t offset = mpegAddr & 0x1FFFFFF;
        uint32_t* data = reinterpret_cast<uint32_t*>(rdram + offset);

        out << "  CMpeg @ 0x" << std::hex << mpegAddr << std::dec << " {" << std::endl;
        out << "    oid_1:      0x" << std::hex << data[0] << std::dec << std::endl;
        out << "    oid_2:      0x" << std::hex << data[1] << std::dec << std::endl;
        out << "    pvBuffer:   0x" << std::hex << data[2] << std::dec << std::endl;
        out << "    cbBuffer:   " << data[3] << " bytes" << std::endl;
        out << "    mpeg:       0x" << std::hex << data[4] << std::dec << std::endl;

        // Dump first 64 bytes for analysis
        out << "    Raw data (first 64 bytes):" << std::endl << "      ";
        for (int i = 0; i < 16; i++) {
            out << std::hex << std::setw(8) << std::setfill('0') << data[i] << " ";
            if ((i + 1) % 4 == 0 && i < 15) out << std::endl << "      ";
        }
        out << std::dec << std::endl << "  }" << std::endl;
    }

    // ExecuteOids DEBUG version - log but don't skip
    void ExecuteOids_debug(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        initMpegLog();

        // $a0 (register 4) contains 'this' pointer to CMpeg object
        uint32_t mpegAddr = getRegU32(ctx, 4);

        static int call_count = 0;
        call_count++;

        // Log every call to console (first 20) and file (all)
        if (call_count <= 20) {
            std::cout << "[MPEG DEBUG #" << call_count << "] ExecuteOids called" << std::endl;
        }

        // Log to file
        if (mpeg_log_file.is_open()) {
            mpeg_log_file << "[ExecuteOids #" << call_count << "] CMpeg @ 0x"
                         << std::hex << mpegAddr << std::dec << std::endl;
            dumpCMpegStruct(rdram, mpegAddr, mpeg_log_file);
            mpeg_log_file.flush();
        }

        // Log every 1000 calls to console
        if (call_count % 1000 == 0) {
            std::cout << "[MPEG DEBUG] ExecuteOids called " << call_count << " times" << std::endl;
        }

        // DON'T return - let the real recompiled code run!
        // We just log and pass through to the actual function
        // The actual function will be called after this stub returns via fallthrough

        // Actually, we need to call the REAL function since we're replacing it
        // Get the original function from the runtime and call it
        // For now, let's just log and return without modifying anything
        // This will show us what the function expects

        // Return to caller WITHOUT modifying CMpeg - let real behavior happen
        ctx->pc = getRegU32(ctx, 31);
    }

    // sceMpegIsEnd DEBUG version - log the actual state
    void sceMpegIsEnd_debug(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        initMpegLog();

        // $a0 contains pointer to sceMpeg structure
        uint32_t mpegHandle = getRegU32(ctx, 4);

        static int call_count = 0;
        call_count++;

        // Read some values from the MPEG handle to understand state
        int isEnd = 0;  // Default: not ended
        if (mpegHandle >= 0x100000 && mpegHandle < 0x2000000) {
            uint32_t offset = mpegHandle & 0x1FFFFFF;
            // Try to determine MPEG state from structure
            // This is PS2 SDK internal - we need to figure out the format
        }

        // Log to console (first 20)
        if (call_count <= 20) {
            std::cout << "[MPEG DEBUG #" << call_count << "] sceMpegIsEnd called, handle=0x"
                     << std::hex << mpegHandle << std::dec
                     << " -> returning 0 (not ended)" << std::endl;
        }

        // Log to file
        if (mpeg_log_file.is_open()) {
            mpeg_log_file << "[sceMpegIsEnd #" << call_count << "] handle=0x"
                         << std::hex << mpegHandle << std::dec << std::endl;

            // Dump handle structure
            if (mpegHandle >= 0x100000 && mpegHandle < 0x2000000) {
                uint32_t offset = mpegHandle & 0x1FFFFFF;
                uint32_t* data = reinterpret_cast<uint32_t*>(rdram + offset);
                mpeg_log_file << "  sceMpeg handle data:" << std::endl << "    ";
                for (int i = 0; i < 8; i++) {
                    mpeg_log_file << std::hex << std::setw(8) << std::setfill('0') << data[i] << " ";
                }
                mpeg_log_file << std::dec << std::endl;
            }
            mpeg_log_file.flush();
        }

        // Return 0 - video NOT ended (let it keep trying)
        // This is the opposite of the skip stub - we want to see what happens
        setReturnS32(ctx, 0);

        // Return to caller
        ctx->pc = getRegU32(ctx, 31);
    }
}
