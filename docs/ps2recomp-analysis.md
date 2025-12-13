# PS2Recomp - Volledige Analyse

> **Datum:** December 2024
> **Project:** Sly Cooper Static Recompilation
> **Status:** Experimenteel / Work-in-Progress

---

## Inhoudsopgave

1. [Wat is Static Recompilation?](#wat-is-static-recompilation)
2. [Repository Structuur](#repository-structuur)
3. [Component Status](#component-status)
4. [Onze Fixes](#onze-fixes)
5. [Wat Werkt](#wat-werkt)
6. [Wat Mist](#wat-mist)
7. [Vergelijking met N64Recomp](#vergelijking-met-n64recomp)
8. [Technische Details](#technische-details)
9. [Conclusie](#conclusie)

---

## Wat is Static Recompilation?

Static recompilation vertaalt console game code naar native PC code **vooraf** (niet tijdens runtime zoals een emulator):

```
┌─────────────┐     ┌──────────────┐     ┌─────────────┐     ┌──────────┐
│ PS2 ELF     │ ──► │ ps2recomp    │ ──► │ C++ code    │ ──► │ Native   │
│ (MIPS R5900)│     │ (translator) │     │ (generated) │     │ .exe     │
└─────────────┘     └──────────────┘     └─────────────┘     └──────────┘
     Game            Recompiler          30MB source         Windows exe
```

### Voordelen t.o.v. Emulatie:
- **Sneller** - Native CPU instructies, geen interpretatie
- **Eenvoudiger** - Geen cycle-accurate CPU emulatie nodig
- **Moddable** - C++ source code is leesbaar/aanpasbaar

### Nadelen:
- **Per-game werk** - Elke game heeft eigen configuratie nodig
- **Incomplete** - Hardware moet nog steeds gesimuleerd worden
- **Complex** - Vereist diep begrip van beide platforms

---

## Repository Structuur

```
ps2recomp/
├── ps2xRecomp/           # Main recompiler tool
│   ├── src/
│   │   ├── main.cpp              # Entry point
│   │   ├── ps2_recompiler.cpp    # Main recompilation logic
│   │   ├── r5900_decoder.cpp     # MIPS instruction decoder
│   │   ├── code_generator.cpp    # C++ code generation
│   │   └── elf_parser.cpp        # ELF file parsing
│   └── include/
│       └── ps2recomp/
│           ├── instructions.h    # 776 lines - all MIPS opcodes
│           ├── code_generator.h  # Translation method declarations
│           └── types.h           # Data structures
│
├── ps2xRuntime/          # Runtime library (PS2 simulation)
│   ├── src/
│   │   ├── lib/
│   │   │   ├── ps2_runtime.cpp   # Core runtime + syscall dispatch
│   │   │   ├── ps2_memory.cpp    # Memory management
│   │   │   ├── ps2_syscalls.cpp  # Syscall implementations
│   │   │   └── ps2_stubs.cpp     # C library stubs
│   │   └── runner/
│   │       ├── main.cpp          # Game runner entry point
│   │       ├── ps2_recompiled_functions.cpp  # Generated (30MB!)
│   │       └── register_functions.cpp        # Function registration
│   └── include/
│       ├── ps2_runtime.h         # 528 lines - core structures
│       ├── ps2_runtime_macros.h  # MIPS operation macros
│       ├── ps2_syscalls.h        # Syscall declarations
│       └── ps2_stubs.h           # Stub declarations
│
├── ps2xAnalyzer/         # ELF analysis tool
│   └── src/
│       └── main.cpp              # Auto-generates config from ELF
│
├── CMakeLists.txt        # Build configuration
├── README.md             # "Not ready - experimental project"
└── build/
    └── _deps/            # External dependencies
        ├── elfio/        # ELF file parsing
        ├── toml11/       # Config file parsing
        ├── fmt/          # String formatting
        └── raylib/       # Graphics/windowing
```

---

## Component Status

### Overzicht Tabel

| Component | Status | Percentage | Notes |
|-----------|--------|------------|-------|
| **Instruction Decoder** | ✅ Werkt | 90% | R5900, MMI, VU0, FPU compleet |
| **ELF Parser** | ✅ Werkt | 95% | ELFIO library, betrouwbaar |
| **Code Generator** | ⚠️ Deels | 70% | Basis werkt, bugs gefixt |
| **Memory Manager** | ✅ Werkt | 85% | 32MB RAM, scratchpad, TLB |
| **Syscalls** | ⚠️ Deels | 30% | Basics werken, veel stubs |
| **Graphics (GS)** | ❌ Stub | 5% | Alleen register structs |
| **DMA Controller** | ❌ Stub | 5% | Structs zonder logica |
| **VIF/VU1** | ❌ Stub | 5% | Niet geïmplementeerd |
| **Audio (SPU2)** | ❌ Geen | 0% | Volledig afwezig |
| **Threading** | ⚠️ Basis | 20% | Alleen ID tracking |
| **Input** | ❌ Geen | 0% | Controller niet geïmplementeerd |

### Detail per Component

#### Instruction Decoder (R5900Decoder)
**Status: COMPLEET**

Ondersteunt alle PS2-specifieke instructies:
- Standard MIPS: ADD, SUB, LOAD, STORE, BRANCH, JUMP
- MMI (Multimedia): PADDW, PSUBW, PMADDW, PCPYLD, etc.
- VU0 Macro Mode: 128-bit vector operaties
- FPU (COP1): Floating point
- COP0: System control

```cpp
// Voorbeeld decoded instructie
Instruction {
    address: 0x100008,
    raw: 0x3c020028,
    opcode: OPCODE_LUI,
    rt: 2,
    immediate: 0x0028,
    hasDelaySlot: false
}
```

#### Code Generator
**Status: WERKT MET FIXES**

Vertaalt MIPS naar C++:

```cpp
// MIPS: addiu $sp, $sp, -16
// Wordt:
SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 4294967280));

// MIPS: sw $ra, 0($sp)
// Wordt:
WRITE32(ADD32(GPR_U32(ctx, 29), 0), GPR_U32(ctx, 31));
```

**Problemen die wij hebben gefixt:**
- Delay slots over functie-grenzen
- Reserved C++ identifiers
- Branch target labels

#### Memory Management
**Status: GOED**

```cpp
// Gedefinieerde geheugenregio's
PS2_RAM_SIZE = 32 * 1024 * 1024;      // 32MB main RAM
PS2_SCRATCHPAD_SIZE = 16 * 1024;       // 16KB scratchpad
PS2_IOP_RAM_SIZE = 2 * 1024 * 1024;    // 2MB IOP RAM
PS2_BIOS_SIZE = 4 * 1024 * 1024;       // 4MB BIOS

// Memory regions
PS2_RAM_BASE = 0x00000000;
PS2_SCRATCHPAD_BASE = 0x70000000;
PS2_IO_BASE = 0x10000000;
PS2_BIOS_BASE = 0x1FC00000;
```

#### Syscalls
**Status: DEELS GEÏMPLEMENTEERD**

| Syscall | Nummer | Status |
|---------|--------|--------|
| SetupThread | 0x3C | ✅ Gefixt door ons |
| SetupHeap | 0x3D | ✅ Werkt |
| FlushCache | 0x64 | ✅ No-op |
| CreateSema | 0x40 | ⚠️ Basis |
| WaitSema | 0x44 | ⚠️ Stub |
| CreateThread | 0x20 | ⚠️ Stub |
| StartThread | 0x22 | ⚠️ Stub |
| SifLoadModule | 0x86 | ❌ Stub |
| GsSetCrt | 0x02 | ❌ Stub |

---

## Onze Fixes

Tijdens het testen van Sly Cooper hebben wij de volgende bugs gevonden en gefixt:

### 1. JAL Return Addresses (KRITIEK)
**Probleem:** Wanneer `JAL` (Jump And Link) wordt uitgevoerd, zet het `RA = PC + 8`. Bij return (`jr $ra`) moet dit adres een geregistreerde functie zijn.

**Oplossing:** Script gemaakt dat alle 18.111 JAL return adressen vindt en als entry points toevoegt.

```python
# analyze_jal_returns.py
# Scant ELF voor JAL instructies, extraheert return adressen
# Voegt ze toe aan sly1_functions.json
```

**Impact:** Van 4.682 naar 22.416 functies.

### 2. SetupThread Syscall (KRITIEK)
**Probleem:** Syscall 0x3C returned verkeerde waarde.

```cpp
// FOUT - returned thread ID
setReturnS32(ctx, 1);

// CORRECT - returned stack pointer
uint32_t stack_ptr = (stack == -1)
    ? 0x02000000 - stack_size
    : stack + stack_size;
setReturnU32(ctx, stack_ptr);
```

**Impact:** Stack was corrupt (0xFFFFFFF1 i.p.v. 0x64C700).

### 3. Delay Slot Handling (KRITIEK)
**Probleem:** Functie-grenzen sneden delay slots af.

```
Functie A: 0x1f9ed8 - 0x1f9ee0
  0x1f9ed8: lw $ra, 0($sp)
  0x1f9edc: j target        # Branch

Functie B: 0x1f9ee0 - 0x1f9ee8
  0x1f9ee0: addiu $sp, 16   # DELAY SLOT - hoort bij functie A!
```

**Oplossing:** Script dat functie-grenzen aanpast om delay slots mee te nemen.

### 4. Reserved C++ Identifiers
**Probleem:** Functies als `__is_pointer` zijn reserved in C++.

```cpp
// Sanitize function names
if (name.size() >= 2 && name[0] == '_' && name[1] == '_') {
    return "fn_" + name;  // __is_pointer -> fn___is_pointer
}
```

### 5. BIOS Syscall Routing
**Probleem:** Game probeert BIOS syscall handlers aan te roepen (0x80xxxxxx).

```cpp
// Check for BIOS addresses
if (handler >= 0x80000000) {
    // Stub BIOS calls
    setReturnS32(ctx, 0);
}
```

---

## Wat Werkt

### Huidige Executie Status

```
Boot Sequence:
✅ _start (0x100008)
✅ BSS clearing loop
✅ SetupThread syscall → SP = 0x64C700
✅ SetupHeap syscall → heap configured
✅ _InitSys
✅ supplement_crt0
✅ GetSystemCallTableEntry
✅ InitAlarm
✅ InitThread
✅ InitExecPS2
✅ Syscall table registration (8 syscalls)
✅ FlushCache
⏳ main() entry... → CRASH na 108 calls
```

### Test Output

```
=== Call #1 ===
  PC: 0x100008
  SP: 0x2000000

=== SYSCALL 60 (SetupThread) ===
  -> SP=0x64c700 ✓

=== Call #108 ===
  PC: 0x185ba0

No func at 0x185bc8  ← Huidige crash
```

---

## Wat Mist

### Must Have (voor speelbare game)

#### 1. Graphics Synthesizer (GS)
De PS2 GPU. Zonder dit: zwart scherm.

```cpp
// Huidige staat - alleen structs
struct GSRegisters {
    uint64_t pmode;
    uint64_t display1, display2;
    uint64_t bgcolor;
    // ... meer registers
};

// Nodig: volledige GS emulatie
// - Primitive rendering (triangles, sprites)
// - Texture mapping
// - Frame buffer management
// - CLUT handling
```

**Geschatte werk:** 2-4 maanden

#### 2. DMA Controller
Directe geheugen transfers tussen componenten.

```cpp
// 10 DMA channels nodig
struct DMARegisters {
    uint32_t chcr;  // Control
    uint32_t madr;  // Memory address
    uint32_t qwc;   // Quadword count
    uint32_t tadr;  // Tag address
};
```

**Geschatte werk:** 2-4 weken

#### 3. VIF (Vector Interface)
Unpacks data voor VU processing.

**Geschatte werk:** 2-3 weken

#### 4. Complete Syscalls
Vooral threading en synchronisatie.

**Geschatte werk:** 1-2 weken

### Nice to Have

| Feature | Beschrijving | Werk |
|---------|--------------|------|
| Audio (SPU2) | Sound output | 1-2 maanden |
| Controller | Gamepad input | 1 week |
| Memory Card | Save/load | 1 week |
| CD/DVD | Disc access | 2 weken |

---

## Vergelijking met N64Recomp

PS2Recomp is geïnspireerd door [N64Recomp](https://github.com/Mr-Wiseguy/N64Recomp).

| Aspect | N64Recomp | PS2Recomp |
|--------|-----------|-----------|
| **Maturiteit** | Production-ready | Experimental |
| **Games** | Meerdere werkend | Geen volledig |
| **Community** | Actief | Minimaal |
| **Documentatie** | Goed | Basis |
| **Hardware sim** | Compleet | 20-30% |
| **CPU** | MIPS R4300i | MIPS R5900 |
| **Complexiteit** | Medium | Hoog (128-bit, VU) |

### Waarom PS2 Moeilijker Is

1. **128-bit registers** - N64 heeft 64-bit, PS2 heeft 128-bit MMI
2. **Vector Units** - VU0/VU1 zijn complete co-processors
3. **Complexere GPU** - GS is veel geavanceerder dan RDP
4. **Meer RAM** - 32MB vs 4MB betekent meer data
5. **Minder documentatie** - PS2 is minder reverse-engineered

---

## Technische Details

### R5900 Context Structure

```cpp
struct R5900Context {
    // 32 General Purpose Registers (128-bit elk!)
    __m128i r[32];

    // Program Counter
    uint32_t pc;

    // Multiply/Divide results
    uint64_t hi, lo;      // Primair
    uint64_t hi1, lo1;    // Secundair (PS2-specifiek)

    // Shift Amount
    uint32_t sa;

    // VU0 Registers (macro mode)
    __m128 vu0_vf[32];    // Vector float registers
    uint16_t vi[16];       // Vector integer registers
    float vu0_q, vu0_p;    // Division results
    __m128 vu0_acc;        // Accumulator

    // COP0 System Control
    uint32_t cop0_status;
    uint32_t cop0_cause;
    uint32_t cop0_epc;
    // ... 20+ meer registers

    // FPU (COP1)
    float f[32];
    uint32_t fcr31;
};
```

### Memory Access Macros

```cpp
// Lezen
#define READ8(addr)  (*(uint8_t*)((rdram) + ((addr) & 0x1FFFFFF)))
#define READ32(addr) (*(uint32_t*)((rdram) + ((addr) & 0x1FFFFFF)))
#define READ128(addr) (*((__m128i*)((rdram) + ((addr) & 0x1FFFFFF))))

// Schrijven
#define WRITE32(addr, val) (*(uint32_t*)((rdram) + ((addr) & 0x1FFFFFF)) = (val))

// Register access
#define GPR_U32(ctx, reg) ((reg == 0) ? 0U : M128I_U32(ctx->r[reg], 0))
#define SET_GPR_S32(ctx, reg, val) \
    do { if (reg != 0) ctx->r[reg] = _mm_set_epi32(0,0,0,(val)); } while(0)
```

### Build Configuratie

```toml
# sly1_config.toml
[general]
input = "disc/SCUS_971.98"
output = "recomp_output/"
single_file_output = true
functions = "sly1_functions.json"

stubs = [
    "printf", "sprintf", "malloc", "free",
    "memcpy", "memset", "strlen", "strcpy"
]

skip = [
    "abort", "exit", "_exit",
    "__start", "_ftext",
    "__do_global_dtors", "__do_global_ctors"
]
```

---

## Conclusie

### Project Status

**PS2Recomp is een experimenteel project op ~25% completie.**

De architectuur is solide en geïnspireerd door het succesvolle N64Recomp project, maar de implementatie is verre van af. Wij zijn effectief de eerste serieuze testers en hebben meerdere kritieke bugs gevonden en gefixt.

### Wat We Hebben Bereikt

- ✅ Game boot sequence werkt
- ✅ 108 functies worden uitgevoerd
- ✅ Stack en heap correct geïnitialiseerd
- ✅ Syscall framework functioneel
- ✅ 22.416 functies succesvol gerecompileerd

### Wat Nog Nodig Is

Voor een **speelbare** Sly Cooper port:

1. **Graphics Synthesizer emulatie** (2-4 maanden werk)
2. **DMA/VIF implementatie** (1 maand)
3. **Complete syscall support** (2 weken)
4. **Audio** (1-2 maanden)
5. **Input handling** (1 week)

### Aanbeveling

Dit project heeft potentie, maar vereist significante investering om tot een werkend product te komen. De huidige staat is geschikt voor:

- Leren over PS2 architectuur
- Experimenteren met static recompilation
- Bijdragen aan open-source development

Niet geschikt voor:
- Daadwerkelijk games spelen
- Production gebruik

---

## Referenties

### Inspiratie & Documentatie
- [N64Recomp](https://github.com/Mr-Wiseguy/N64Recomp) - Inspiratie project
- [PS2Tek](https://psi-rockin.github.io/ps2tek/) - PS2 hardware documentatie
- [PS2SDK](https://github.com/ps2dev/ps2sdk) - PS2 development kit
- [PCSX2](https://pcsx2.net/) - PS2 emulator (referentie implementatie)

### Referentie Projecten (zie `reference/` directory)

We hebben nuttige referentie-implementaties verzameld van andere PS2 recompilatie projecten:

| Project | Locatie | Nuttig voor |
|---------|---------|-------------|
| **parallel-gs** | `reference/parallel-gs/` | GS emulatie (Vulkan compute shaders) |
| **ps2-recompiler-Cactus** | `reference/ps2-recompiler-Cactus/` | pthread threading, mutex semaphores, syscalls |
| **Ps2Recomp-menaman123** | `reference/Ps2Recomp-menaman123/` | SDL-based Event Flags |
| **SotCStaticRecompilation** | `reference/SotCStaticRecompilation/` | Config voorbeelden |

#### parallel-gs (voor Graphics)
Volledige PS2 Graphics Synthesizer emulator in Vulkan:
```cpp
// Key interface: gs/gs_interface.hpp
class GSInterface {
    void gif_transfer(uint32_t path, const void *data, size_t size);
    void write_register(RegisterAddr addr, uint64_t payload);
    ScanoutResult vsync(const VSyncInfo &info);
};
```

#### ps2-recompiler-Cactus (voor Threading)
Echte pthread threading implementatie:
```c
// src/ps2lib/threading/threading.c
void Recomp_StartThread(int threadId, void *arg) {
    pthread_create(&thread, NULL, (void*)&_StartThread, &data);
}

// src/ps2lib/threading/semaphor.c
int Kernel_WaitSema(int id) {
    pthread_mutex_lock(&ctx->ee_semaphors[id].mutex);
}
```

Zie `reference/README.md` voor volledige documentatie van alle referentie projecten.

---

*Gegenereerd tijdens Sly Cooper recompilation debugging sessie*
*Laatste update: December 2024 - Referentie projecten toegevoegd*
