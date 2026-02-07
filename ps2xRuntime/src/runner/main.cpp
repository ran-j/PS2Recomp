#include "ps2_runtime.h"
#include "register_functions.h"
#include <iostream>
#include <string>

static void printUsage(const char *exe)
{
    std::cout << "Usage: " << exe << " <elf_file>\n"
              << "       " << exe << " --help\n";
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printUsage(argv[0]);
        return 1;
    }

    const std::string firstArg = argv[1];
    if (firstArg == "-h" || firstArg == "--help")
    {
        printUsage(argv[0]);
        return 0;
    }

    const std::string elfPath = firstArg;

    PS2Runtime runtime;
    if (!runtime.initialize("ps2xRuntime (Raylib host)"))
    {
        std::cerr << "Failed to initialize PS2 runtime" << std::endl;
        return 1;
    }

    registerAllFunctions(runtime);

    if (!runtime.loadELF(elfPath))
    {
        std::cerr << "Failed to load ELF file: " << elfPath << std::endl;
        return 1;
    }

    runtime.run();

    return 0;
}
