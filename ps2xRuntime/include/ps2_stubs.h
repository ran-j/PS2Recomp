#ifndef PS2_STUBS_H
#define PS2_STUBS_H

#include "ps2_runtime.h"
#include <cstdint>

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
}

#endif // PS2_STUBS_H
