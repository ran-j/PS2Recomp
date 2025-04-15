#ifndef PS2_STUBS_H
#define PS2_STUBS_H

#include "ps2_runtime.h"

// Standard library implementation stubs
namespace ps2_stubs
{
    // Memory operations
    void memcpy(uint8_t *rdram, R5900Context *ctx);
    void memset(uint8_t *rdram, R5900Context *ctx);
    void memmove(uint8_t *rdram, R5900Context *ctx);
    void memcmp(uint8_t *rdram, R5900Context *ctx);

    // String operations
    void strcpy(uint8_t *rdram, R5900Context *ctx);
    void strncpy(uint8_t *rdram, R5900Context *ctx);
    void strlen(uint8_t *rdram, R5900Context *ctx);
    void strcmp(uint8_t *rdram, R5900Context *ctx);
    void strncmp(uint8_t *rdram, R5900Context *ctx);

    // I/O operations
    void printf(uint8_t *rdram, R5900Context *ctx);
    void sprintf(uint8_t *rdram, R5900Context *ctx);
    void puts(uint8_t *rdram, R5900Context *ctx);

    // Memory allocation
    void malloc(uint8_t *rdram, R5900Context *ctx);
    void free(uint8_t *rdram, R5900Context *ctx);
    void calloc(uint8_t *rdram, R5900Context *ctx);
    void realloc(uint8_t *rdram, R5900Context *ctx);

    // Math functions
    void sqrt(uint8_t *rdram, R5900Context *ctx);
    void sin(uint8_t *rdram, R5900Context *ctx);
    void cos(uint8_t *rdram, R5900Context *ctx);
    void atan2(uint8_t *rdram, R5900Context *ctx);
}

// PS2 system call implementation stubs
namespace ps2_syscalls
{
    void FlushCache(uint8_t *rdram, R5900Context *ctx);
    void ExitThread(uint8_t *rdram, R5900Context *ctx);
    void SleepThread(uint8_t *rdram, R5900Context *ctx);
    void CreateThread(uint8_t *rdram, R5900Context *ctx);
    void StartThread(uint8_t *rdram, R5900Context *ctx);
    void SifInitRpc(uint8_t *rdram, R5900Context *ctx);
    void SifBindRpc(uint8_t *rdram, R5900Context *ctx);
    void SifCallRpc(uint8_t *rdram, R5900Context *ctx);
    void SetGsCrt(uint8_t *rdram, R5900Context *ctx);
}

// MMI operation implementations
namespace ps2_mmi
{
    void PADDW(R5900Context *ctx, int rd, int rs, int rt);
    void PSUBW(R5900Context *ctx, int rd, int rs, int rt);
    void PCGTW(R5900Context *ctx, int rd, int rs, int rt);
    void PMAXW(R5900Context *ctx, int rd, int rs, int rt);
    void PADDH(R5900Context *ctx, int rd, int rs, int rt);
    void PSUBH(R5900Context *ctx, int rd, int rs, int rt);
    void PCGTH(R5900Context *ctx, int rd, int rs, int rt);
    void PMAXH(R5900Context *ctx, int rd, int rs, int rt);
    void PADDB(R5900Context *ctx, int rd, int rs, int rt);
    void PSUBB(R5900Context *ctx, int rd, int rs, int rt);
    void PCGTB(R5900Context *ctx, int rd, int rs, int rt);

    void PEXTLW(R5900Context *ctx, int rd, int rs, int rt);
    void PEXTUW(R5900Context *ctx, int rd, int rs, int rt);
    void PEXTLH(R5900Context *ctx, int rd, int rs, int rt);
    void PEXTUH(R5900Context *ctx, int rd, int rs, int rt);
    void PEXTLB(R5900Context *ctx, int rd, int rs, int rt);
    void PEXTUB(R5900Context *ctx, int rd, int rs, int rt);

    void PMADDW(R5900Context *ctx, int rd, int rs, int rt);
    void PMSUBW(R5900Context *ctx, int rd, int rs, int rt);
    void PMADDH(R5900Context *ctx, int rd, int rs, int rt);
    void PMSUBH(R5900Context *ctx, int rd, int rs, int rt);

    void PAND(R5900Context *ctx, int rd, int rs, int rt);
    void POR(R5900Context *ctx, int rd, int rs, int rt);
    void PXOR(R5900Context *ctx, int rd, int rs, int rt);
    void PNOR(R5900Context *ctx, int rd, int rs, int rt);
}

// Hardware register handlers
namespace ps2_hardware
{
    // DMA channel handlers
    void handleDmacReg(uint32_t address, uint32_t value);

    // GS register handlers
    void handleGsReg(uint32_t address, uint32_t value);

    // VIF register handlers
    void handleVif0Reg(uint32_t address, uint32_t value);
    void handleVif1Reg(uint32_t address, uint32_t value);

    void handleTimerReg(uint32_t address, uint32_t value);
    void handleSPU2Reg(uint32_t address, uint32_t value);
}

#endif // PS2_RUNTIME_H