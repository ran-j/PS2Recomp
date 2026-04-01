# Runtime Library
The runtime library provides the execution environment for recompiled code, including:

* Memory management (32MB main RAM, scratchpad, etc.)
* Register context (128-bit GPRs, VU0 registers, etc.)
* Function table for dynamic linking
* Basic PS2 system call stubs

# How to use:

Take your decompiled code and place the cpp files on ps2xRuntime/src/runner and header files on ps2xRuntime/include and compile/be happy.

## Vita Build Notes

The Vita runtime uses `Quenom/raylib-5.5-vita` for vita build. I recommend build runtime only.

Expected environment:

* `VITASDK` points to your VitaSDK root.
* `Quenom/raylib-5.5-vita` has already been built and installed into `$VITASDK/arm-vita-eabi`.
* SDL2 with the PVR backend required by that raylib fork is also installed into the same VitaSDK prefix.
* `PS2X_DEFAULT_BOOT_ELF` is mandatory, you need to define where your game is like "ux0:data/RANJ00001/game/SLUS_201.84".

The CMake for `ps2xRuntime` consumes those preinstalled headers and libraries from VitaSDK. It does not fetch or install the Vita raylib fork for you.

## Adding Custom Function Implementations
You can add custom implementations for PS2 system calls or game functions by:

1. Creating function implementations that match the signature:
```cpp
void function_name(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime);
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
