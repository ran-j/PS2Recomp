# PS2Recomp Bug Report & Fixes

> **Tester:** Chris
> **Test Game:** Sly Cooper and the Thievius Raccoonus (SCUS_971.98)
> **Date:** December 2024
> **ps2recomp commit:** c05d8f1 (latest as of testing)

---

## Executive Summary

During testing of ps2recomp with Sly Cooper, we discovered and fixed **5 critical bugs** that prevented the game from booting. After our fixes, the game successfully executes 108+ function calls before hitting unimplemented hardware features.

**Result:** Game progresses from immediate crash to partial boot sequence completion.

---

## Bug #1: JR $ra Not Setting ctx->pc (CRITICAL)

### Problem

When a function returns via `jr $ra`, the code generator only emitted `return;` without setting `ctx->pc`. This breaks the run loop dispatch mechanism.

### Location

`ps2xRecomp/src/code_generator.cpp` - `handleBranchDelaySlots()` function

### Original Code

```cpp
if (rs_reg == 31 && branchInst.function == SPECIAL_JR)
{
    ss << "    return;\n";
}
else
{
    ss << "    ctx->pc = GPR_U32(ctx, " << (int)rs_reg << "); return;\n";
}
```

### Problem Analysis

The run loop works like this:
```cpp
while (!shouldExit) {
    func = lookupFunction(ctx->pc);
    func(rdram, ctx, runtime);
}
```

When `jr $ra` just does `return;` without setting `ctx->pc`, the run loop doesn't know where to continue execution. The next iteration uses stale/uninitialized `ctx->pc`.

### Fix

```cpp
// All jr instructions should set ctx->pc before returning
// This allows the run loop to know where to continue execution
ss << "    ctx->pc = GPR_U32(ctx, " << (int)rs_reg << "); return;\n";
```

### Impact

**Without fix:** Game crashes immediately after first function return.
**With fix:** Run loop correctly dispatches to return address.

---

## Bug #2: SetupThread Syscall Returns Wrong Value (CRITICAL)

### Problem

Syscall 0x3C (SetupThread/InitMainThread) returned thread ID instead of stack pointer.

### Location

`ps2xRuntime/src/lib/ps2_runtime.cpp` - `handleSyscall()` case 0x3c

### Original Code

```cpp
case 0x3c: // SetupThread
    {
        // SetupThread(ThreadParam *param, void *stack)
        // Returns thread ID (use 1 for main thread)
        std::cout << "  -> SetupThread: Returning thread ID 1" << std::endl;
        setReturnS32(ctx, 1);
    }
    break;
```

### Problem Analysis

According to PS2 SDK documentation (ps2tek), `InitMainThread` signature is:
```c
void* InitMainThread(uint32 gp, void* stack, int stack_size, char* args, int root)
```

**Returns:** Stack pointer of the thread
- If `stack == -1`: `SP = end_of_RDRAM - stack_size`
- Else: `SP = stack + stack_size`

The original code returned `1` (thread ID), but the game uses the return value to set `$sp`:
```mips
0x100064: syscall            ; SetupThread
0x100068: daddu $sp, $v0, $0 ; SP = return value!
```

With `$v0 = 1`, the stack pointer became `0x00000001`, causing immediate crash.

### Fix

```cpp
case 0x3c: // SetupThread / InitMainThread (RFU060)
    {
        // InitMainThread(uint32 gp, void* stack, int stack_size, char* args, int root)
        // Returns: stack pointer of the thread
        uint32_t gp = M128I_U32(ctx->r[4], 0);
        int32_t stack = (int32_t)M128I_U32(ctx->r[5], 0);
        int32_t stack_size = (int32_t)M128I_U32(ctx->r[6], 0);

        uint32_t stack_ptr;
        if (stack == -1) {
            stack_ptr = 0x02000000 - stack_size;
        } else {
            stack_ptr = (uint32_t)stack + stack_size;
        }

        // Also set GP register
        ctx->r[28] = _mm_set_epi32(0, 0, 0, gp);
        setReturnU32(ctx, stack_ptr);
    }
    break;
```

### Impact

**Without fix:** `SP = 0x00000001` - immediate stack corruption and crash.
**With fix:** `SP = 0x64c700` (correct value for Sly Cooper).

### Reference

- [PS2Tek - EE Syscalls](https://israpps.github.io/ps2tek/PS2/BIOS/EE_Syscalls.html)

---

## Bug #3: Reserved C++ Identifiers Not Sanitized (CRITICAL)

### Problem

Some PS2 game symbols use names that are reserved in C++, causing compilation failures.

### Affected Names

| Original Name | Problem | Fix |
|--------------|---------|-----|
| `__is_pointer` | Reserved (starts with `__`) | `fn___is_pointer` |
| `__cp_pop_exception` | Reserved (starts with `__`) | `fn___cp_pop_exception` |
| `_InitSys` | Reserved (`_` + uppercase) | `fn__InitSys` |
| `main` | Conflicts with program entry | `game_main` |
| `matherr` | Conflicts with C library | `fn_matherr` |

### Location

`ps2xRecomp/src/code_generator.cpp` - new `sanitizeFunctionName()` helper
`ps2xRecomp/src/ps2_recompiler.cpp` - same helper needed for header generation

### Fix

```cpp
static std::string sanitizeFunctionName(const std::string& name) {
    if (name == "main") return "game_main";
    if (name == "matherr") return "fn_matherr";

    // Prefix names starting with double underscore (reserved in C++)
    if (name.size() >= 2 && name[0] == '_' && name[1] == '_') {
        return "fn_" + name;
    }

    // Also handle names starting with underscore followed by uppercase
    if (name.size() >= 2 && name[0] == '_' && std::isupper(name[1])) {
        return "fn_" + name;
    }

    return name;
}
```

### Impact

**Without fix:** Compilation fails with reserved identifier errors.
**With fix:** All functions compile successfully.

---

## Bug #4: Delay Slots Split Across Function Boundaries (CRITICAL)

### Problem

When functions are split at JAL return addresses, the delay slot instruction can end up in a separate function, causing it to never execute.

### Example

Original MIPS code:
```mips
0x1f9ed8: ld   $ra, 0($sp)      ; Load return address
0x1f9edc: j    entry_1fa8f8     ; Jump (has delay slot)
0x1f9ee0: addiu $sp, $sp, 16    ; DELAY SLOT - restores stack!
```

After function splitting:
```
Function: _InitSys_cont_..._cont_1f9ed8
  Address: 0x1f9ed8 - 0x1f9ee0  (does NOT include 0x1f9ee0!)

Function: entry_1f9ee0
  Address: 0x1f9ee0 - 0x1f9ee8
```

The delay slot (`addiu $sp, $sp, 16`) is in a separate function and never executes!

### Generated Code (Broken)

```cpp
void fn__InitSys_cont_..._cont_1f9ed8(...) {
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 0))); // Load RA
    entry_1fa8f8(rdram, ctx, runtime); return;                 // Jump - NO DELAY SLOT!
}

void entry_1f9ee0(...) {
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 16));         // Never reached!
}
```

### Solution

We created a Python script (`fix_delay_slot_splits.py`) that:
1. Scans the ELF for all branch/jump instructions
2. Identifies functions that end with a branch
3. Extends those functions by 4 bytes to include the delay slot
4. Shrinks the next function accordingly

### Script Output

```
Found 10 functions that need delay slot fixes

Fixes applied:
  _InitSys_cont_..._cont_1f9ed8 @ 0x1f9edc -> delay slot 0x1f9ee0
  entry_1f6f68 @ 0x1f6f6c -> delay slot 0x1f6f70
  ... (8 more)
```

### Alternative Fix (Code Generator)

Could also be fixed in the code generator by reading the delay slot instruction even if it's outside the function boundary. This requires access to raw ELF bytes during code generation.

### Impact

**Without fix:** Stack not restored, return to wrong address, crash.
**With fix:** Delay slots execute correctly.

---

## Bug #5: Missing Syscall Handlers (MEDIUM)

### Problem

Several syscalls used by the game were not implemented or returned wrong values.

### Missing Syscalls Implemented

#### Syscall 0x5A/0x5B - GetSyscallHandler

The game queries the current handler for each syscall (to clone the BIOS syscall table).

```cpp
case 0x5a:
case 0x5b:
    {
        // Returns the current handler address for the requested syscall
        uint32_t requested_syscall = M128I_U32(ctx->r[4], 0);
        // Return a unique BIOS-like address for each syscall
        uint32_t handler_addr = 0x80070000 + (requested_syscall * 8);
        setReturnU32(ctx, handler_addr);
    }
    break;
```

#### Syscall 0x74 - SetSyscall

The game registers custom syscall handlers.

```cpp
case 0x74:
    {
        uint32_t syscall_to_set = M128I_U32(ctx->r[4], 0);
        uint32_t handler_addr = M128I_U32(ctx->r[5], 0);
        m_customSyscalls[syscall_to_set] = handler_addr;
        setReturnS32(ctx, 0);
    }
    break;
```

#### BIOS Handler Detection

When the game tries to call a BIOS syscall handler (address >= 0x80000000), we stub it instead of crashing:

```cpp
if (handler >= 0x80000000) {
    // This is a BIOS syscall handler, stub it
    setReturnS32(ctx, 0);
} else if (lookupFunction(handler) != nullptr) {
    ctx->pc = handler;  // Dispatch to game handler
} else {
    setReturnS32(ctx, 0);  // Unknown handler, stub
}
```

---

## Additional Fixes

### Cross-Platform __m128i Access

The original macros used MSVC-specific syntax for accessing __m128i elements:

```cpp
// Original (MSVC only)
#define GPR_U32(ctx, reg) ctx->r[reg].m128i_u32[0]
```

Fix adds GCC/Clang compatibility:

```cpp
#ifdef _MSC_VER
    #define M128I_U32(v, i) ((v).m128i_u32[i])
#else
    union m128i_union { __m128i v; uint32_t u32[4]; ... };
    inline uint32_t m128i_get_u32(__m128i v, int i) {
        m128i_union u; u.v = v; return u.u32[i];
    }
    #define M128I_U32(v, i) m128i_get_u32(v, i)
#endif
```

### SSE4.1 Compiler Flags

Added missing compiler flags for 128-bit operations:

```cmake
# ps2xRuntime/CMakeLists.txt
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ps2_runtime PRIVATE -msse4.1)
    target_compile_options(ps2EntryRunner PRIVATE -msse4.1)
endif()
```

### Internal Branch Labels

Added support for internal function branches using labels:

```cpp
// Collect branch targets within function
void CodeGenerator::collectBranchTargets(const std::vector<Instruction> &instructions) {
    for (const auto &inst : instructions) {
        if (inst.isBranch) {
            uint32_t target = inst.address + 4 + (inst.simmediate << 2);
            if (target >= m_currentFuncStart && target < m_currentFuncEnd) {
                m_internalBranchTargets.insert(target);
            }
        }
    }
}

// Emit labels for branch targets
if (m_internalBranchTargets.count(inst.address)) {
    ss << "label_" << std::hex << inst.address << std::dec << ":\n";
}
```

---

## Files Modified

| File | Changes |
|------|---------|
| `ps2xRecomp/src/code_generator.cpp` | +182 lines (sanitize names, branch labels, delay slots, jr fix) |
| `ps2xRecomp/src/ps2_recompiler.cpp` | +47 lines (sanitize names in headers) |
| `ps2xRecomp/include/ps2recomp/code_generator.h` | +9 lines (new member variables) |
| `ps2xRuntime/src/lib/ps2_runtime.cpp` | +556/-340 lines (syscalls, debug output) |
| `ps2xRuntime/src/lib/ps2_syscalls.cpp` | +71 lines (helper functions) |
| `ps2xRuntime/src/runner/main.cpp` | +77 lines (run loop, debug) |
| `ps2xRuntime/CMakeLists.txt` | +6 lines (SSE4.1 flags) |

## Scripts Created

| Script | Purpose |
|--------|---------|
| `analyze_jal_returns.py` | Finds all JAL return addresses and adds as entry points |
| `fix_delay_slot_splits.py` | Fixes function boundaries that split delay slots |
| `add_entry.py` | Manually add single entry point |
| `iterate_fixes.py` | Automated test-fix-rebuild loop |

---

## Test Results

### Before Fixes

```
Game start: CRASH
Error: "No func at 0x0"
Functions executed: 0
```

### After Fixes

```
Game start: SUCCESS
Boot sequence:
  ✓ _start
  ✓ SetupThread (SP = 0x64c700)
  ✓ SetupHeap
  ✓ _InitSys
  ✓ Syscall table initialization
  ✓ FlushCache
Functions executed: 108+
Current blocker: Missing hardware emulation (GS/DMA)
```

---

## Recommendations

1. **Integrate sanitizeFunctionName()** - Add to both code_generator.cpp and ps2_recompiler.cpp
2. **Fix SetupThread syscall** - Critical for any game to boot
3. **Add delay slot boundary check** - Either in function splitter or code generator
4. **Document jr $ra behavior** - Make clear that ctx->pc must always be set
5. **Add more syscalls** - Many games need 0x5a/0x5b for syscall table cloning

---

## Contact

These fixes were developed while testing Sly Cooper recompilation. Happy to provide more details or help integrate these changes.

**Repository tested:** ps2recomp (commit c05d8f1)
**Test environment:** Windows 11, MSYS2/MinGW64, GCC 13
