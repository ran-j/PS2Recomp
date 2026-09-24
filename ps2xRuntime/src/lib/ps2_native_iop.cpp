#include "runtime/ps2_native_iop.h"

#include "ps2_iop_transport.h"
#include "raylib.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace ps2_native_iop
{
    namespace
    {
        // raylib's stream callback carries no user pointer.
        std::atomic<ps2x::iop::NativeIop *> g_source{nullptr};
        AudioStream g_stream{};
        bool g_streaming = false;
        float g_volume = 1.0f;

        void fill(void *buffer, unsigned int frames)
        {
            if (ps2x::iop::NativeIop *source = g_source.load(std::memory_order_acquire))
                source->render(static_cast<int16_t *>(buffer), frames);
            // PS2X_AUDIO_DUMP=file keeps a copy of everything played, as raw
            // 48 kHz stereo s16, to compare against a reference offline.
            static FILE *dump = [] {
                const char *path = std::getenv("PS2X_AUDIO_DUMP");
                return path && *path ? std::fopen(path, "wb") : nullptr;
            }();
            if (dump)
                std::fwrite(buffer, 4u, frames, dump);
        }
    }

    void setModules(PS2Runtime &runtime, std::vector<std::string> names)
    {
        if (ps2x::iop::NativeIop *native = PS2IopTransport::native(&runtime))
            native->setModules(std::move(names));
    }

    void setVolume(float volume)
    {
        g_volume = volume;
#if !defined(PLATFORM_VITA)
        if (g_streaming)
            SetAudioStreamVolume(g_stream, g_volume);
#endif
    }

    void startAudio(PS2Runtime &runtime)
    {
#if !defined(PLATFORM_VITA)
        ps2x::iop::NativeIop *native = PS2IopTransport::native(&runtime);
        if (g_streaming || !native || !IsAudioDeviceReady())
            return;
        // 1024 frames is about 21 ms, and the IOP renders a buffer in well under that.
        SetAudioStreamBufferSizeDefault(1024);
        g_stream = LoadAudioStream(48000u, 16u, 2u);
        if (!IsAudioStreamValid(g_stream))
        {
            std::cerr << "[iop] could not open an audio stream; the native IOP runs silently" << std::endl;
            return;
        }
        g_source.store(native, std::memory_order_release);
        SetAudioStreamCallback(g_stream, &fill);
        SetAudioStreamVolume(g_stream, g_volume);
        PlayAudioStream(g_stream);
        g_streaming = true;
#else
        (void)runtime;
#endif
    }

    void stopAudio()
    {
#if !defined(PLATFORM_VITA)
        if (!g_streaming)
            return;
        // Unloading takes the mixer lock, so no callback is running after it.
        UnloadAudioStream(g_stream);
        g_source.store(nullptr, std::memory_order_release);
        g_streaming = false;
#endif
    }
}
