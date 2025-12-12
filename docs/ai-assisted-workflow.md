# AI-Assisted Reverse Engineering Workflow

> **Author:** Chris King (@chrisking1981)
> **AI Tool:** Claude Code with Claude Opus 4.5
> **Project:** PS2 Static Recompilation for Sly Cooper
> **Date:** December 2024

---

## Overview

This document describes how I use AI (specifically Claude Code with Opus 4.5) to systematically debug and fix complex reverse engineering projects. This workflow was developed while testing and fixing [PS2Recomp](https://github.com/ran-j/PS2Recomp), a PS2 static recompilation tool.

**Results achieved:**
- Found and fixed 5 critical bugs
- Game went from 0 function calls (immediate crash) to 108+ function calls
- Created detailed bug reports with exact code fixes
- All done in collaborative sessions with AI

---

## The Workflow

### 1. Start with a Clear Goal

```
User: "I want to test ps2recomp with Sly Cooper and see if it works"
```

Don't try to do everything at once. Start with a specific, testable goal.

### 2. Let AI Explore the Codebase

Claude Code can read files, search code, and understand project structure. I ask it to:

```
User: "Explore the ps2recomp repository and explain what each component does"
```

The AI will:
- Read README files and documentation
- Examine source code structure
- Identify key files and their purposes
- Report back with a summary

### 3. Build and Test

```
User: "Build the project and run it"
```

The AI can execute build commands and capture output:
- Runs cmake/make/ninja
- Captures compiler errors
- Identifies missing dependencies
- Suggests fixes for build issues

### 4. Systematic Debugging ("Think Like a Reverse Engineer")

When something crashes, I prompt Claude to think systematically:

```
User: "The game crashes. Think like a real reverse engineer -
       test everything systematically so we know what's working"
```

This triggers methodical analysis:
1. **Identify the crash point** - What address? What function?
2. **Examine register state** - Are values correct?
3. **Trace backwards** - What led to this state?
4. **Form hypotheses** - Why might this happen?
5. **Test hypotheses** - Add debug output, check assumptions
6. **Fix and verify** - Apply fix, confirm it works

### 5. Iterative Problem Solving

Each bug fix often reveals the next issue. The workflow becomes:

```
Test → Crash → Analyze → Fix → Test → New Crash → Analyze → Fix → ...
```

Example session progression:
```
Crash #1: "No func at 0x0"
  → Fix: jr $ra must set ctx->pc

Crash #2: "SP = 0x00000001" (corrupt stack)
  → Fix: SetupThread must return stack pointer, not thread ID

Crash #3: Compiler error "__is_pointer reserved"
  → Fix: Sanitize reserved C++ identifiers

Crash #4: Stack not restored after function call
  → Fix: Delay slots split across function boundaries

Crash #5: "No func at 0x185bc8"
  → Analysis: Missing entry points, need to add JAL return addresses
```

### 6. Create Scripts for Repetitive Tasks

When we identified that 18,000+ JAL return addresses were missing, Claude created Python scripts:

```python
# analyze_jal_returns.py - Scans ELF for all JAL instructions
# fix_delay_slot_splits.py - Fixes function boundary issues
# iterate_fixes.py - Automated test-fix-rebuild loop
```

This automates tedious work that would take hours manually.

### 7. Document Everything

After fixing issues, I ask Claude to create documentation:

```
User: "Create a detailed bug report for the ps2recomp maintainer"
```

This produces:
- Exact description of each bug
- Code snippets showing before/after
- Explanation of root cause
- Test results proving the fix works

---

## Key Prompts That Work Well

### For Exploration
```
"Explore the codebase and explain the architecture"
"What does this function do? Read it and explain"
"Search for all places where X is used"
```

### For Debugging
```
"Think like a reverse engineer - what could cause this?"
"Test systematically, don't guess"
"What's the value of SP at this point? Trace it back"
```

### For Fixing
```
"Fix this bug, show me the exact code change"
"Create a script to automate this"
"What's the minimal change needed?"
```

### For Documentation
```
"Document what we found for the maintainer"
"Create a bug report with all the details"
"Explain this so someone else can understand"
```

---

## Example Session: Finding the SetupThread Bug

Here's a real example of how we found a critical bug:

**1. Observed Problem:**
```
=== Call #2 ===
  SP: 0xfffffff1   ← This is wrong! Should be ~0x2000000
```

**2. AI Analysis:**
```
The stack pointer is corrupt. Let me trace where SP gets set...

Looking at _start:
  0x100064: syscall        ; SetupThread
  0x100068: daddu $sp, $v0, $0  ; SP = return value of syscall!

So SP is set from $v0, which comes from SetupThread syscall.
```

**3. Found the Bug:**
```cpp
// Current code (WRONG):
case 0x3c: // SetupThread
    setReturnS32(ctx, 1);  // Returns thread ID = 1
```

**4. Researched Correct Behavior:**
```
AI searched PS2 SDK documentation and found:
- SetupThread returns STACK POINTER, not thread ID
- Formula: SP = stack + stack_size (or 0x02000000 - stack_size if stack == -1)
```

**5. Applied Fix:**
```cpp
// Fixed code:
case 0x3c: // SetupThread
    uint32_t stack_ptr = (stack == -1)
        ? 0x02000000 - stack_size
        : stack + stack_size;
    setReturnU32(ctx, stack_ptr);  // Returns stack pointer
```

**6. Verified:**
```
=== Call #2 ===
  SP: 0x64c700   ← Correct!
```

---

## Tools Used

| Tool | Purpose |
|------|---------|
| **Claude Code** | AI assistant (Claude Opus 4.5 model) |
| **VS Code / Terminal** | Code editing and command execution |
| **Git** | Version control, tracking changes |
| **Python** | Scripting for automation |
| **MSYS2/MinGW** | Windows build environment |

---

## Tips for AI-Assisted Reverse Engineering

### Do:
- ✅ Give clear, specific goals
- ✅ Ask AI to "think systematically"
- ✅ Let AI read actual source code (not just describe)
- ✅ Build iteratively - fix one thing at a time
- ✅ Ask for documentation of findings
- ✅ Create scripts for repetitive tasks

### Don't:
- ❌ Expect AI to guess without information
- ❌ Skip the exploration phase
- ❌ Try to fix everything at once
- ❌ Ignore AI's questions or suggestions
- ❌ Forget to test after each fix

---

## Results Summary

**Project:** PS2Recomp (static recompilation tool)
**Test Game:** Sly Cooper and the Thievius Raccoonus

| Metric | Before | After |
|--------|--------|-------|
| Function calls | 0 | 108+ |
| Boot sequence | Crash | Completes |
| Bugs found | - | 5 critical |
| Scripts created | 0 | 6 |
| Documentation | None | Full bug report |

**Time spent:** ~4-6 hours of collaborative AI sessions
**Equivalent manual time:** Estimated 40-80 hours

---

## Files Created During This Project

```
chris-docs/
├── ai-assisted-reverse-engineering.md  # This document
├── ps2recomp-analysis.md               # Full project analysis
└── ps2recomp-bug-report.md             # Detailed bug report

tools/ps2recomp/
├── analyze_jal_returns.py              # Find JAL return addresses
├── fix_delay_slot_splits.py            # Fix function boundaries
├── add_entry.py                        # Add single entry point
├── iterate_fixes.py                    # Automated test loop
└── sly1_functions.json                 # 22,416 function definitions
```

---

## Contributing Back

After finding and fixing bugs, the workflow for contributing back:

1. **Fork the original repo** on GitHub
2. **Create a feature branch** for your fixes
3. **Commit with detailed messages** explaining each fix
4. **Push to your fork**
5. **Create a Pull Request** with:
   - Summary of bugs found
   - Test results (before/after)
   - Link to detailed documentation

Our PR: https://github.com/ran-j/PS2Recomp/pull/XXX

---

## Conclusion

AI-assisted reverse engineering dramatically accelerates the debugging process. The key is treating the AI as a collaborative partner:

- **You** provide direction and domain knowledge
- **AI** provides systematic analysis, code reading, and automation

Together, you can tackle complex projects that would otherwise take weeks or months.

---

*This workflow was developed by Chris King using Claude Code (Opus 4.5). Feel free to adapt it for your own projects.*
