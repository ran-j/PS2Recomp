#include "ps2recomp/ps2_recompiler.h"
#include <iostream>
#include <string>

using namespace ps2recomp;

void printUsage()
{
    std::cout << "PS2Recomp - A static recompiler for PlayStation 2 ELF files\n";
    std::cout << "Usage: ps2recomp <config.toml> [--emit-manifest-only]\n";
    std::cout << "  config.toml: Configuration file for the recompiler\n";
    std::cout << "  --emit-manifest-only: run only the analysis phase (decode + emit this\n";
    std::cout << "    unit's external_call_targets.txt) and exit before ingesting any sibling\n";
    std::cout << "    manifest. For a multi-unit build, run every unit with this flag first\n";
    std::cout << "    (any order), then run every unit normally.\n";
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printUsage();
        return 1;
    }

    std::string configPath = argv[1];
    bool emitManifestOnly = false;
    for (int i = 2; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--emit-manifest-only")
        {
            emitManifestOnly = true;
        }
    }

    try
    {
        PS2Recompiler recompiler(configPath);

        if (!recompiler.initialize())
        {
            std::cerr << "Failed to initialize recompiler\n";
            recompiler.printReport();
            return 1;
        }

        if (!recompiler.recompile(emitManifestOnly))
        {
            std::cerr << "Recompilation failed\n";
            recompiler.printReport();
            return 1;
        }

        if (emitManifestOnly)
        {
            recompiler.printReport();
            std::cout << "Analysis phase completed successfully\n";
            return 0;
        }

        recompiler.generateOutput();
        recompiler.printReport();

        std::cout << "Recompilation completed successfully\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}