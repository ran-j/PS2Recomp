#include "ps2_runtime.h"
#include "register_functions.h"
#include "games_database.h"

#include <iostream>
#include <string>
#include <filesystem>

std::string normalizeGameId(const std::string& folderName)
{
    std::string result = folderName;

    size_t underscore = result.find('_');
    if (underscore != std::string::npos)
        result[underscore] = '-';

    size_t dot = result.find('.');
    if (dot != std::string::npos)
        result.erase(dot, 1);

    return result;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "Usage: " << argv[0] << " <elf_file>" << std::endl;
        return 1;
    }

    std::string elfPath = argv[1];
    std::filesystem::path pathObj(elfPath);
    std::string folderName = pathObj.filename().string();
    std::string normalizedId = normalizeGameId(folderName);

    std::string windowTitle = "PS2-Recomp | ";
    const char* gameName = getGameName(normalizedId);

    if (gameName)
    {
        windowTitle += std::string(gameName) + " | " + folderName;
    }
    else
    {
        windowTitle += folderName;
    }

    PS2Runtime runtime;
    if (!runtime.initialize(windowTitle.c_str()))
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