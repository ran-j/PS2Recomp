// Sly Cooper Recompiled - Main Entry Point
#include <iostream>
#include <string>
#include <filesystem>

#include "ps2_runtime.h"

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

    // Register all recompiled functions with the runtime
    std::cout << "Registering recompiled functions..." << std::endl;
    registerAllFunctions(runtime);
    std::cout << "Functions registered." << std::endl;

    // Start execution
    std::cout << "Starting game execution..." << std::endl;
    runtime.run();

    std::cout << "Execution completed." << std::endl;
    return 0;
}
