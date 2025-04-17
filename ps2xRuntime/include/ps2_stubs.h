#ifndef PS2_STUBS_H
#define PS2_STUBS_H

#include "ps2_runtime.h"
#include <cstdint>

namespace ps2_stubs
{

    // Memory operations
    void malloc(uint8_t *rdram, R5900Context *ctx);
    void free(uint8_t *rdram, R5900Context *ctx);
    void calloc(uint8_t *rdram, R5900Context *ctx);
    void realloc(uint8_t *rdram, R5900Context *ctx);
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
    void strcat(uint8_t *rdram, R5900Context *ctx);
    void strncat(uint8_t *rdram, R5900Context *ctx);
    void strchr(uint8_t *rdram, R5900Context *ctx);
    void strrchr(uint8_t *rdram, R5900Context *ctx);
    void strstr(uint8_t *rdram, R5900Context *ctx);

    // I/O operations
    void printf(uint8_t *rdram, R5900Context *ctx);
    void sprintf(uint8_t *rdram, R5900Context *ctx);
    void snprintf(uint8_t *rdram, R5900Context *ctx);
    void puts(uint8_t *rdram, R5900Context *ctx);
    void fopen(uint8_t *rdram, R5900Context *ctx);
    void fclose(uint8_t *rdram, R5900Context *ctx);
    void fread(uint8_t *rdram, R5900Context *ctx);
    void fwrite(uint8_t *rdram, R5900Context *ctx);
    void fprintf(uint8_t *rdram, R5900Context *ctx);
    void fseek(uint8_t *rdram, R5900Context *ctx);
    void ftell(uint8_t *rdram, R5900Context *ctx);
    void fflush(uint8_t *rdram, R5900Context *ctx);

    // Math functions
    void sqrt(uint8_t *rdram, R5900Context *ctx);
    void sin(uint8_t *rdram, R5900Context *ctx);
    void cos(uint8_t *rdram, R5900Context *ctx);
    void tan(uint8_t *rdram, R5900Context *ctx);
    void atan2(uint8_t *rdram, R5900Context *ctx);
    void pow(uint8_t *rdram, R5900Context *ctx);
    void exp(uint8_t *rdram, R5900Context *ctx);
    void log(uint8_t *rdram, R5900Context *ctx);
    void log10(uint8_t *rdram, R5900Context *ctx);
    void ceil(uint8_t *rdram, R5900Context *ctx);
    void floor(uint8_t *rdram, R5900Context *ctx);
    void fabs(uint8_t *rdram, R5900Context *ctx);

    void TODO(uint8_t *rdram, R5900Context *ctx);
}

#endif // PS2_STUBS_H
