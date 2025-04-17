#include "ps2_runtime.h"
#include "register_functions.h"
#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cout << "Usage: " << argv[0] << " <elf_file>" << std::endl;
        return 1;
    }

    std::string elfPath = argv[1];

    PS2Runtime runtime;
    if (!runtime.initialize())
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