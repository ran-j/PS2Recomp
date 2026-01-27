#ifndef PS2_SYSCALLS_H
#define PS2_SYSCALLS_H

#include "ps2_runtime.h"
#include "ps2_call_list.h"
#include <mutex>
#include <atomic>

// Number of active host threads spawned for PS2 thread emulation
extern std::atomic<int> g_activeThreads;

static std::mutex g_sys_fd_mutex;

#define PS2_FIO_O_RDONLY 0x0001
#define PS2_FIO_O_WRONLY 0x0002
#define PS2_FIO_O_RDWR 0x0003
#define PS2_FIO_O_NBLOCK 0x0010
#define PS2_FIO_O_APPEND 0x0100
#define PS2_FIO_O_CREAT 0x0200
#define PS2_FIO_O_TRUNC 0x0400
#define PS2_FIO_O_EXCL 0x0800
#define PS2_FIO_O_NOWAIT 0x8000

#define PS2_SEEK_SET 0
#define PS2_SEEK_CUR 1
#define PS2_SEEK_END 2

namespace ps2_syscalls
{
    #define PS2_DECLARE_SYSCALL(name) void name(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    PS2_SYSCALL_LIST(PS2_DECLARE_SYSCALL)
    #undef PS2_DECLARE_SYSCALL

    void TODO(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}

#endif // PS2_SYSCALLS_H
