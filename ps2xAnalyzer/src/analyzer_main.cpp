#include "ps2recomp/elf_analyzer.h"
#include <iostream>
#include <string>

void printUsage()
{
    std::cout << "PS2 ELF Analyzer\n";
    std::cout << "A tool to analyze PS2 ELF files and generate TOML configuration for PS2Recomp\n\n";
    std::cout << "Usage: ps2_analyzer <input_elf> <output_toml>\n";
    std::cout << "  input_elf    Path to the PS2 ELF file\n";
    std::cout << "  output_toml  Path to output TOML configuration file\n";
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printUsage();
        return 1;
    }

    std::string elfPath = argv[1];
    std::string tomlPath = argv[2];

    std::cout << "PS2 ELF Analyzer\n";
    std::cout << "----------------\n";
    std::cout << "Input ELF: " << elfPath << "\n";
    std::cout << "Output TOML: " << tomlPath << "\n\n";

    try
    {
        ps2recomp::ElfAnalyzer analyzer(elfPath);
 
        if (!analyzer.analyze())
        {
            std::cerr << "Failed to analyze ELF file\n";
            return 1;
        }
 
        if (!analyzer.generateToml(tomlPath))
        {
            std::cerr << "Failed to generate TOML configuration\n";
            return 1;
        }

        std::cout << "\nAnalysis complete\n";
        std::cout << "TOML configuration has been written to: " << tomlPath << "\n";
        std::cout << "\nYou can now use this configuration with PS2Recomp:\n";
        std::cout << "  ps2recomp " << tomlPath << "\n";

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}