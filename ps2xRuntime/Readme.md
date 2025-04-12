# Runtime Library
The runtime library provides the execution environment for recompiled code, including:

* Memory management (32MB main RAM, scratchpad, etc.)
* Register context (128-bit GPRs, VU0 registers, etc.)
* Function table for dynamic linking
* Basic PS2 system call stubs

## Adding Custom Function Implementations
You can add custom implementations for PS2 system calls or game functions by:

1. Creating function implementations that match the signature:
```cpp
void function_name(uint8_t* rdram, R5900Context* ctx);
```

2. Registering them with the runtime:
```cpp
runtime.registerFunction(address, function_name);
```

## Advanced Features
Memory Translation
The runtime handles PS2's memory addressing, including:

* KSEG0/KSEG1 direct mapping
* TLB lookups for user memory
* Special memory areas (scratchpad, I/O registers)

## Vector Unit Support
PS2-specific 128-bit MMI instructions and VU0 macro mode instructions are supported via SSE/AVX intrinsics.

## Instruction Patching
You can patch specific instructions in the recompiled code to fix game issues or implement custom behavior.

## Limitations

* Graphics and sound output require external implementations
* Some PS2-specific hardware features may not be fully supported
* Performance may vary based on the complexity of the game