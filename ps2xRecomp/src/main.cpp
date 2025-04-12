#include "ps2recomp/ps2_recompiler.h"
#include <iostream>
#include <string>

using namespace ps2recomp;

void printUsage()
{
    std::cout << "PS2Recomp - A static recompiler for PlayStation 2 ELF files\n";
    std::cout << "Usage: ps2recomp <config.toml>\n";
    std::cout << "  config.toml: Configuration file for the recompiler\n";
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printUsage();
        return 1;
    }

    std::string configPath = argv[1];

    try
    {
        PS2Recompiler recompiler(configPath);

        if (!recompiler.initialize())
        {
            std::cerr << "Failed to initialize recompiler\n";
            return 1;
        }

        if (!recompiler.recompile())
        {
            std::cerr << "Recompilation failed\n";
            return 1;
        }

        recompiler.generateOutput();

        std::cout << "Recompilation completed successfully\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}