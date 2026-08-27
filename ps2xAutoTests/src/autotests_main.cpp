#include "ps2_runtime.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: " << argv[0] << " <test.elf>" << std::endl;
        return 2;
    }

    PS2Runtime runtime;

    if (!runtime.initialize("ps2xAutoTests", true))
    {
        std::cerr << "Failed to initialize PS2 runtime" << std::endl;
        return 2;
    }

    if (!runtime.loadELF(argv[1]))
    {
        std::cerr << "Failed to load ELF file: " << argv[1] << std::endl;
        return 2;
    }

    runtime.run();

    std::cout.flush();
    std::fflush(stdout);

    std::_Exit(0);
}
