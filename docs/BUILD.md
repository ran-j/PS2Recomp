# Build Instructies voor PS2Recomp

> **Platform:** Windows met MSYS2/MinGW64
> **Laatst getest:** December 2024

---

## Vereisten

### Software
- **MSYS2** met MinGW64 toolchain (`C:\msys64\`)
- **CMake** 3.20+ (meegeleverd met MSYS2)
- **Ninja** build system (meegeleverd met MSYS2)
- **Git** (meegeleverd met MSYS2)

### MSYS2 Packages
```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
```

---

## Project Structuur

```
sly1-ps2-decomp/
├── PS2Recomp/              # Static recompiler en runtime
│   ├── build/              # Build output directory
│   ├── ps2xRecomp/         # Recompiler tool
│   ├── ps2xRuntime/        # Runtime library en runner
│   └── ps2xAnalyzer/       # ELF analyzer
├── sly1-recomp/            # Game-specifieke configuratie
│   └── config/
│       ├── sly1_config.toml    # Recompiler config
│       └── sly1_functions.json # Function definitions
├── sly1-decomp/            # Sly Cooper decomp project
│   └── disc/
│       └── SCUS_971.98     # Game ELF (NTSC-U)
├── recomp_output/          # Recompiled C++ output
├── reference/              # Reference implementations
├── docs/                   # Documentatie
└── tools/                  # Helper scripts
```

---

## Build Stappen

### Belangrijk: PATH Setup

De MSYS2 toolchain vereist **beide** directories in PATH:
- `/c/msys64/mingw64/bin` - Compilers (gcc, g++, cmake, ninja)
- `/c/msys64/usr/bin` - Git helpers (basename, sed, etc.)

**In Claude Code of Git Bash:**
```bash
export PATH="/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH"
```

**In PowerShell:**
```powershell
$env:PATH = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;$env:PATH"
```

### 1. Clean Build (eerste keer of na problemen)

```bash
cd PS2Recomp

# Verwijder oude build
rm -rf build
mkdir build
cd build

# Configure met MSYS2 compilers
PATH="/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH" \
  /c/msys64/mingw64/bin/cmake.exe -G Ninja \
  -DFETCHCONTENT_UPDATES_DISCONNECTED=ON \
  -DCMAKE_BUILD_TYPE=Release \
  ..
```

**Opties:**
- `-DFETCHCONTENT_UPDATES_DISCONNECTED=ON` - Voorkomt git fetch errors
- `-DCMAKE_BUILD_TYPE=Release` - Optimized build (sneller)

### 2. Incrementele Build

```bash
cd PS2Recomp/build

PATH="/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH" \
  /c/msys64/mingw64/bin/ninja.exe ps2EntryRunner
```

**Build targets:**
- `ps2EntryRunner` - Runtime + runner executable
- `ps2recomp` - Recompiler tool
- `ps2_analyzer` - ELF analyzer
- `all` - Alles

### 3. Recompile Game Functions

Als je `sly1_functions.json` hebt aangepast:

```bash
cd sly1-recomp/config

# Run recompiler
../../PS2Recomp/build/ps2xRecomp/ps2recomp.exe sly1_config.toml

# BELANGRIJK: Kopieer output naar runtime directory!
cp ../../recomp_output/ps2_recompiled_functions.cpp ../../PS2Recomp/ps2xRuntime/src/runner/
cp ../../recomp_output/ps2_recompiled_functions.h ../../PS2Recomp/ps2xRuntime/include/
cp ../../recomp_output/register_functions.cpp ../../PS2Recomp/ps2xRuntime/src/runner/
```

Dit genereert nieuwe bestanden in `recomp_output/` die je MOET kopiëren naar de runtime directories.

### 4. Run de Game

```bash
cd PS2Recomp/build/ps2xRuntime

./ps2EntryRunner.exe "../../../sly1-decomp/disc/SCUS_971.98"
```

**Met trace logging:**
```bash
./ps2EntryRunner.exe "../../../sly1-decomp/disc/SCUS_971.98" 2>&1 | tee ../../../trace.log
```

---

## Veelvoorkomende Problemen

### Problem: "remote helper 'https' aborted session"

**Oorzaak:** Git kan geen https fetch doen door ontbrekende helpers.

**Oplossing:** Voeg `/c/msys64/usr/bin` toe aan PATH:
```bash
export PATH="/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH"
```

### Problem: "basename: opdracht niet gevonden"

**Oorzaak:** MSYS2 usr/bin niet in PATH.

**Oplossing:** Zelfde als hierboven.

### Problem: CMake kan compiler niet vinden

**Symptoom:** Exit code 1 zonder error message.

**Oplossing:** Gebruik volledige pad naar cmake:
```bash
/c/msys64/mingw64/bin/cmake.exe -G Ninja ..
```

### Problem: Ninja rebuild faalt met FetchContent errors

**Oorzaak:** CMake probeert dependencies te updaten.

**Oplossing:**
1. Clean build met `-DFETCHCONTENT_UPDATES_DISCONNECTED=ON`
2. Of touch de stamp files:
```bash
touch _deps/*/src/*-stamp/*-update
```

### Problem: "No func at 0xXXXXXX"

**Oorzaak:** Missing function entry point.

**Oplossing:**
1. Zoek de functie in `sly1_functions.json`
2. Mogelijk moet je een functie splitsen
3. Recompile met ps2recomp
4. Rebuild runtime

---

## Development Workflow

### Nieuwe functie entry point toevoegen

1. **Identificeer het adres** uit de error message
2. **Check sly1_functions.json** of er een functie is die dit adres bevat
3. **Split de functie** als nodig:
   ```json
   // Origineel:
   {"name": "entry_185ba0", "address": 1596320, "size": 36}

   // Gesplit:
   {"name": "entry_185ba0", "address": 1596320, "size": 16},
   {"name": "entry_185bb0", "address": 1596336, "size": 20}
   ```
4. **Recompile:**
   ```bash
   cd sly1-recomp/config
   ../../PS2Recomp/build/ps2xRecomp/ps2recomp.exe sly1_config.toml
   ```
5. **Rebuild runtime:**
   ```bash
   cd PS2Recomp/build
   PATH="/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH" ninja ps2EntryRunner
   ```
6. **Test:**
   ```bash
   cd ps2xRuntime
   ./ps2EntryRunner.exe "../../../sly1-decomp/disc/SCUS_971.98"
   ```

### Trace Analysis

```bash
# Capture trace
./ps2EntryRunner.exe "../../../sly1-decomp/disc/SCUS_971.98" > ../../../trace.log 2>&1

# Analyze
cd ../../..
python tools/trace_analyzer.py trace.log
```

---

## Build Output

Na succesvolle build vind je:

```
PS2Recomp/build/
├── ps2xRecomp/
│   └── ps2recomp.exe       # Recompiler tool
├── ps2xRuntime/
│   ├── ps2EntryRunner.exe  # Game runner
│   └── libps2_runtime.a    # Runtime library
└── ps2xAnalyzer/
    └── ps2_analyzer.exe    # ELF analyzer
```

---

## Tips voor AI Assistants

1. **Altijd PATH instellen** voordat je build commands runt
2. **Check bestaande build** voordat je clean build doet
3. **Use `-j` voor parallel builds** als je haast hebt:
   ```bash
   ninja -j8 ps2EntryRunner
   ```
4. **Log alles** bij debugging:
   ```bash
   ninja ps2EntryRunner 2>&1 | tee build.log
   ```

---

*Laatst bijgewerkt: December 2024*
