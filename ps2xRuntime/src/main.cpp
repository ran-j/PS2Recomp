#include "ps2_runtime.h"
#include "register_functions.h"
#include "games_database.h"

#ifdef _DEBUG
#include "ps2_log.h"
#endif

#include <iostream>
#include <string>
#include <filesystem>
#include <exception>
#include <algorithm>

namespace
{
    void setupTerminateLogger() // to help on release build crashs
    {
        std::set_terminate([]()
                           {
                               std::cerr << "[terminate] unhandled exception" << std::endl;
                               const std::exception_ptr ep = std::current_exception();
                               if (ep)
                               {
                                   try
                                   {
                                       std::rethrow_exception(ep);
                                   }
                                   catch (const std::system_error &e)
                                   {
                                       std::cerr << "[terminate] std::system_error code=" << e.code().value()
                                                 << " category=" << e.code().category().name()
                                                 << " message=" << e.what() << std::endl;
                                   }
                                   catch (const std::exception &e)
                                   {
                                       std::cerr << "[terminate] std::exception: " << e.what() << std::endl;
                                   }
                                   catch (...)
                                   {
                                       std::cerr << "[terminate] non-std exception" << std::endl;
                                   }
                               }
                               std::abort(); });
    }

    std::string normalizeGameId(const std::string &folderName)
    {
        std::string result = folderName;

        size_t underscore = result.find('_');
        if (underscore != std::string::npos)
            result[underscore] = '-';

        size_t dot = result.find('.');
        if (dot != std::string::npos)
            result.erase(dot, 1);

        std::ranges::transform(result, result.begin(), [](unsigned char character)
                               { return static_cast<char>(std::toupper(character)); });

        return result;
    }

    std::filesystem::path getExecutablePath(int argc, char *argv[])
    {
        if (argc >= 2 && argv[1] && argv[1][0] != '\0')
        {
            std::cout << "Using argv boot path" << std::endl;
            return std::filesystem::path(argv[1]);
        }
#if defined(PS2X_DEFAULT_BOOT_ELF)
        std::cout << "Using default boot file" << std::endl;
        const std::filesystem::path configuredPath = std::filesystem::path(PS2X_DEFAULT_BOOT_ELF);
#if defined(PLATFORM_VITA)
        return configuredPath;
#endif
        if (configuredPath.is_absolute())
        {
            return configuredPath;
        }
        return (std::filesystem::current_path() / configuredPath).lexically_normal();
#else
        throw std::runtime_error("Unable to determine executable path. Pass the guest ELF as argv[1] or define PS2X_DEFAULT_BOOT_ELF.");
#endif
    }
}

int main(int argc, char *argv[])
{
    setupTerminateLogger();

    try
    {
        std::filesystem::path pathObj = getExecutablePath(argc, argv);

        std::string filePathStr = pathObj.string();
        std::string elfName = pathObj.filename().string();
        std::string normalizedId = normalizeGameId(elfName);

        std::string windowTitle = "PS2-Recomp | ";
        const char *gameName = getGameName(normalizedId);

#if !defined(PLATFORM_VITA)
        if (gameName)
        {
            windowTitle += std::string(gameName) + " | " + elfName;
        }
        else
#endif
        {
            windowTitle += elfName;
        }

        PS2Runtime runtime;
        if (!runtime.initialize(windowTitle.c_str()))
        {
            std::cerr << "Failed to initialize PS2 runtime" << std::endl;
            return 1;
        }

        registerAllFunctions(runtime);

        if (!runtime.loadELF(filePathStr))
        {
            std::cerr << "Failed to load ELF file: " << filePathStr << std::endl;
            return 1;
        }

        runtime.run();

#ifdef _DEBUG
        ps2_log::print_saved_location();
#endif
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[main] fatal exception: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "[main] fatal exception: unknown" << std::endl;
    }

    return 1;
}
