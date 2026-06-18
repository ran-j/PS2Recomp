# Mid-asm hooks

Inspired by [XenonRecomp](https://github.com/hedge-dev/XenonRecomp)'s `[[midasm_hook]]`,
this lets you inject a call to a hand-written C++ function at an arbitrary **guest
instruction address**, without editing the generated `sub_*.cpp`. It is strictly more
powerful than the existing `[patches]` mechanism (which can only replace a single 32-bit
instruction word).

Use it for surgical fixes the recompiler can't express on its own — patching a
mis-initialized pointer, defusing a NULL-deref, forcing a value, or redirecting control
flow at a known resume point.

## Config

Add one or more `[[midasm_hook]]` array-of-tables entries to the game `.toml`:

```toml
[[midasm_hook]]
name    = "gow_fix_bst_base"   # C++ function you implement in the runtime/runner
address = 0x00175D6C            # guest instruction address to hook (string or int)
after   = false                 # optional: run AFTER the instruction (default: before)
```

- `address` accepts a hex string (`"0x175D6C"`) or an integer.
- `before` (default) runs the hook *before* the instruction at `address`.
- `after = true` runs it *after* a normal (non-branch) instruction.

## What the recompiler emits

For a `before` hook at `0xADDR` it emits, ahead of the instruction:

```cpp
ctx->pc = 0xADDRu;
{ extern void name(uint8_t*, R5900Context*, PS2Runtime*); name(rdram, ctx, runtime); }
if (ctx->pc != 0xADDRu) { return; }   // hook redirected control flow
```

So a hook that only touches state falls through to the original instruction; a hook that
sets `ctx->pc` to a **valid resume point** (a function entry or a recompiled branch/label
target) redirects execution there (the function returns and the dispatcher resumes).

## Implementing a hook

Define the function anywhere that gets linked into the runner (e.g. a new
`ps2xRuntime/src/runner/<game>_hooks.cpp`). The runner globs `src/runner/*.cpp`.

```cpp
#include "ps2_runtime_macros.h"
#include "ps2_runtime.h"

// GoW Layer-5 fix: the BST `base` (mem[0x29C4BC]) was left pointing 8 bytes into the
// pool allocator's arena header; the real pool data start is mem[poolDesc+0x10]
// (poolDesc = mem[0x29C4B8]). Re-point base after the game's (buggy) computation.
extern "C" void gow_fix_bst_base(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime)
{
    uint32_t poolDesc = READ32(0x0029C4B8u);
    if (poolDesc) {
        uint32_t correctBase = READ32(ADD32(poolDesc, 16));
        if (correctBase) WRITE32(0x0029C4BCu, correctBase);
    }
}
```

(Declare it `extern "C"` or plain `void` matching the emitted `extern void name(...)`.)

## Notes / limits

- Hooks are matched by exact instruction address.
- Control-flow redirect only lands cleanly on addresses the function can resume at
  (entry / internal branch targets / resume points). For mid-function jumps that aren't
  resume points, prefer modifying state and falling through.
- `after` hooks are only emitted for non-branch instructions.
