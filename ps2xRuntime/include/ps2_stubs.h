#ifndef PS2_STUBS_H
#define PS2_STUBS_H

#include "ps2_runtime.h"
#include <cstdint>
#include <string>

namespace ps2_stubs
{

    // Memory operations
    void malloc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void free(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void calloc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void realloc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void memcpy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void memset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void memmove(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void memcmp(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    // String operations
    void strcpy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void strncpy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void strlen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void strcmp(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void strncmp(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void strcat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void strncat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void strchr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void strrchr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void strstr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    // I/O operations
    void printf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sprintf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void snprintf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void puts(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void fopen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void fclose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void fread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void fwrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void fprintf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void fseek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void ftell(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void fflush(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    // CD-ROM stubs (bypass loading waits)
    void sceCdSync_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdSyncS_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdGetError_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceCdRead_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    // Debug wrapper for CD init check at 0x203880
    void debug_cd_check_203880(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    // Debug wrapper for entry_203894 - forces CD init to proceed
    void debug_entry_203894(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    // Stub for sceCdDiskReady - bypasses CD init
    void sceCdDiskReady_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    // Stub for sceSifInitRpc - initializes RPC
    void sceSifInitRpc_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    // Stub for entry_2038a4 - bypasses CD init loop
    void debug_entry_2038a4(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    // Stub for entry_203874 - bypasses PollSema loop
    void debug_entry_203874(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    // Sound system stubs - intercept IOP/RPC calls
    void sceSifCheckStatRpc_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void snd_SendIOPCommandAndWait_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void snd_GotReturns_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceSifCallRpc_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    // Math functions
    void sqrt(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sin(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void cos(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void tan(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void atan2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void pow(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void exp(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void log(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void log10(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void ceil(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void floor(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void fabs(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    void TODO(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    // Controller (pad) stubs
    void scePadInit_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadPortOpen_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadGetState_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadRead_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadInfoMode_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadSetMainMode_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadInfoAct_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadSetActAlign_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadSetActDirect_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadInfoPressMode_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePadEnterPressMode_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    // FlushFrames stub
    void FlushFrames_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void WaitSema_func_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void ExecuteOids_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    // sceMpeg stubs - MPEG video system
    void sceMpegIsEnd_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    // MPEG debug versions - log but don't skip, let real code run
    void ExecuteOids_debug(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegIsEnd_debug(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}

// Audio manager functions for real sound playback
namespace audio_manager
{
    void InitializeAudio();
    void SetSoundsPath(const std::string& path);
    bool LoadSoundById(int soundId);
    void PlaySoundById(int soundId, float volume, float pan);
    void StopSoundById(int soundId);
    void Cleanup();
}

#endif // PS2_STUBS_H
