# Bug Report: Function Calls Inside Loops Generate Incorrect Code

## Summary

The PS2Recomp code generator incorrectly generates `return;` after every JAL (Jump And Link) instruction, even when the function call is inside a loop within the same function. This causes loops containing syscalls or function calls to break.

## Affected Component

`ps2xRecomp/src/code_generator.cpp` - `handleBranchDelaySlots()` function

## Problem Description

### Current Behavior

When the recompiler encounters a JAL instruction, it generates:
```cpp
functionName(rdram, ctx, runtime); return;
```

This causes the recompiled function to exit after every function call, breaking loops that contain syscalls.

### Example: FlushFrames Loop

Original MIPS assembly:
```mips
FlushFrames:
    daddu   $16, $0, $0     ; counter = 0
loop:
    jal     WaitSema        ; Call WaitSema
    addiu   $16, $16, 1     ; counter++ (delay slot)
    sltiu   $3, $16, 2      ; check: counter < 2?
    bnez    $3, loop        ; if true, loop back
```

Current (incorrect) generated code:
```cpp
void FlushFrames(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    GPR_S64(ctx, 16) = 0;  // counter = 0
label_loop:
    SET_GPR_U32(ctx, 31, returnAddr);
    GPR_S32(ctx, 16) = GPR_S32(ctx, 16) + 1;
    WaitSema(rdram, ctx, runtime); return;  // BUG: exits function!
}
// Loop never completes because function returns after first call
```

### Expected Behavior

If the return address of a JAL is inside the current function AND is a branch target (part of a loop), the code should continue with a `goto` instead of `return`:

```cpp
void FlushFrames(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    GPR_S64(ctx, 16) = 0;  // counter = 0
label_loop:
    SET_GPR_U32(ctx, 31, returnAddr);
    GPR_S32(ctx, 16) = GPR_S32(ctx, 16) + 1;
    WaitSema(rdram, ctx, runtime); goto label_after_call;  // Continue in loop!
label_after_call:
    // ... rest of loop logic
}
```

## Proposed Fix

### Step 1: Collect JAL return addresses as branch targets

In `collectBranchTargets()`:
```cpp
void CodeGenerator::collectBranchTargets(const std::vector<Instruction> &instructions) {
    // ... existing code ...

    for (const auto &inst : instructions) {
        // ... existing branch target collection ...

        // NEW: Also collect return addresses of JAL instructions
        if (inst.opcode == OPCODE_JAL) {
            uint32_t returnAddr = inst.address + 8;
            if (instructionAddresses.count(returnAddr)) {
                m_internalBranchTargets.insert(returnAddr);
            }
        }
    }
}
```

### Step 2: Use goto instead of return for internal calls

In `handleBranchDelaySlots()`:
```cpp
uint32_t target = (branchInst.address & 0xF0000000) | (branchInst.target << 2);
uint32_t returnAddr = branchInst.address + 8;
Symbol *sym = findSymbolByAddress(target);

// Check if return address is inside current function
bool returnInsideFunction = isInternalBranch(returnAddr);
bool returnAddrIsLabel = m_internalBranchTargets.count(returnAddr) > 0;

if (sym && sym->isFunction) {
    ss << "    " << sanitizeFunctionName(sym->name) << "(rdram, ctx, runtime);";

    // If return address is inside this function and is a branch target,
    // continue execution instead of returning (fixes loops with syscalls)
    if (returnInsideFunction && returnAddrIsLabel) {
        ss << " goto label_" << std::hex << returnAddr << std::dec << ";\n";
    } else {
        ss << " return;\n";
    }
}
```

## Impact

This bug affects any PS2 game that has:
- Loops containing syscalls (WaitSema, SignalSema, etc.)
- Loops containing function calls
- Frame synchronization routines (like FlushFrames)

Games affected include Sly Cooper, and likely many others that use standard PS2 SDK patterns.

## Testing

Tested with Sly Cooper and the Thievius Raccoonus (SCUS_971.98):
- Before fix: Game hangs in FlushFrames loop
- After fix: Game successfully executes 56+ million function calls without hanging

## Files Changed

- `ps2xRecomp/src/code_generator.cpp`:
  - `collectBranchTargets()` - add JAL return addresses to branch targets
  - `handleBranchDelaySlots()` - use goto for internal function calls in loops
