// Sly Cooper Recompiled - Main Entry Point
#include <iostream>
#include <string>
#include <filesystem>

#include "ps2_runtime.h"
#include "ps2_stubs.h"

// Forward declaration for the generated function registration
void registerAllFunctions(PS2Runtime& runtime);

int main(int argc, char* argv[]) {
    std::cout << "=== Sly Cooper and the Thievius Raccoonus (Recompiled) ===" << std::endl;
    std::cout << "PS2 Static Recompilation by PS2Recomp" << std::endl;
    std::cout << std::endl;

    // Create the PS2 runtime
    PS2Runtime runtime;

    // Initialize the runtime with window title
    if (!runtime.initialize("Sly Cooper - Recompiled")) {
        std::cerr << "Failed to initialize PS2 runtime!" << std::endl;
        return 1;
    }

    std::cout << "Runtime initialized successfully." << std::endl;

    // Find the ELF file - from argument or default locations
    std::string elfPath;

    if (argc > 1) {
        elfPath = argv[1];
    } else {
        std::vector<std::string> possiblePaths = {
            "SCUS_971.98",
            "../disc/SCUS_971.98",
            "../../disc/SCUS_971.98",
            "../../../disc/SCUS_971.98",
            "../../../../disc/SCUS_971.98"
        };

        for (const auto& path : possiblePaths) {
            if (std::filesystem::exists(path)) {
                elfPath = path;
                break;
            }
        }
    }

    if (elfPath.empty() || !std::filesystem::exists(elfPath)) {
        std::cerr << "Could not find Sly Cooper ELF file" << std::endl;
        std::cerr << "Usage: " << argv[0] << " <elf_file>" << std::endl;
        std::cerr << "Or place SCUS_971.98 in the disc directory." << std::endl;
        return 1;
    }

    std::cout << "Loading ELF: " << elfPath << std::endl;

    // Load the game's ELF file into memory
    if (!runtime.loadELF(elfPath)) {
        std::cerr << "Failed to load ELF file: " << elfPath << std::endl;
        return 1;
    }

    std::cout << "ELF loaded successfully." << std::endl;
    std::cout << "=== DEBUG BUILD ===" << std::endl; /* DEBUG_INJECT */
    std::cout << "Debug injection active" << std::endl; /* DEBUG_INJECT */

    // Patch CD-ROM status to bypass loading waits
    // The game checks memory at 0x27942C to see if CD is ready (value should be 6)
    uint8_t* rdram = runtime.memory().getRDRAM();
    *reinterpret_cast<uint32_t*>(rdram + 0x27942C) = 6;
    std::cout << "Patched CD status at 0x27942C = 6 (ready)" << std::endl;

    // Patch game state flag at 0x2701b8 to bypass main loop wait
    // This flag is checked in the switch statement at 0x1e91dc
    // When 0, the game loops forever; when non-zero, it proceeds
    *reinterpret_cast<uint32_t*>(rdram + 0x2701b8) = 1;
    std::cout << "Patched game state at 0x2701b8 = 1" << std::endl;

    // NO PATCHES - we want to understand what MPEG needs
    std::cout << "=== NO MPEG PATCHES - ANALYZING FLOW ===" << std::endl;

    // Register all recompiled functions with the runtime
    std::cout << "Registering recompiled functions..." << std::endl;
    registerAllFunctions(runtime);
    std::cout << "Functions registered." << std::endl;

    // Register controller (pad) stubs to override recompiled versions
    std::cout << "Registering controller stubs..." << std::endl;
    runtime.registerFunction(0x2040e8, ps2_stubs::scePadInit_stub);
    runtime.registerFunction(0x2042c8, ps2_stubs::scePadPortOpen_stub);
    runtime.registerFunction(0x204510, ps2_stubs::scePadRead_stub);
    runtime.registerFunction(0x204590, ps2_stubs::scePadGetState_stub);
    runtime.registerFunction(0x2046d0, ps2_stubs::scePadInfoAct_stub);
    runtime.registerFunction(0x2047f8, ps2_stubs::scePadInfoMode_stub);
    runtime.registerFunction(0x204930, ps2_stubs::scePadSetMainMode_stub);
    runtime.registerFunction(0x2049e8, ps2_stubs::scePadSetActDirect_stub);
    runtime.registerFunction(0x204aa8, ps2_stubs::scePadSetActAlign_stub);
    runtime.registerFunction(0x204ce8, ps2_stubs::scePadInfoPressMode_stub);
    runtime.registerFunction(0x204d48, ps2_stubs::scePadEnterPressMode_stub);
    std::cout << "Controller stubs registered." << std::endl;

    // MPEG: Let the real recompiled code run
    // The game may set oid_1 for intro videos - we need Execute to handle them properly
    // sceMpegIsEnd needs to return 1 (ended) when there's no video playing
    runtime.registerFunction(0x20B0B0, ps2_stubs::sceMpegIsEnd_stub); // Return 1 (ended) to skip video wait
    std::cout << "MPEG stub registered (sceMpegIsEnd returns ended)." << std::endl;

    // CD-ROM stubs - override recompiled RPC code with real file access
    runtime.registerFunction(0x203b18, ps2_stubs::sceCdRead_stub);
    runtime.registerFunction(0x2032c8, ps2_stubs::sceCdSync_stub);
    runtime.registerFunction(0x203368, ps2_stubs::sceCdSyncS_stub);
    runtime.registerFunction(0x203d90, ps2_stubs::sceCdGetError_stub);
    std::cout << "CD-ROM stubs registered." << std::endl;

    // Initialize audio system and load sounds
    std::cout << "Initializing audio system..." << std::endl;
    audio_manager::InitializeAudio();

    // Try to find extracted sounds directory
    std::vector<std::string> soundsPaths = {
        "sounds",
        "../sounds",
        "../../sounds",
        "../../../tools/sound_extractor/extracted/wav",
        "../../../../tools/sound_extractor/extracted/wav"
    };

    for (const auto& path : soundsPaths) {
        if (std::filesystem::exists(path)) {
            audio_manager::SetSoundsPath(path);
            std::cout << "Audio sounds path: " << path << std::endl;
            break;
        }
    }

    // Start execution
    std::cout << "Starting game execution..." << std::endl;
    runtime.run();

    std::cout << "Execution completed." << std::endl;
    return 0;
}
