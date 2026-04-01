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

        return result;
    }

    std::filesystem::path getExecutablePath(int argc, char *argv[])
    {
#if defined(PS2X_DEFAULT_BOOT_ELF)
        std::cout << "Using default boot file" << std::endl;
        return std::filesystem::current_path() + PS2X_DEFAULT_BOOT_ELF;
#else
        std::cout << "Using agrs" << std::endl;
        if (argc > 0)
        {
            return std::filesystem::path(argv[0]).parent_path();
        }

        throw std::runtime_error("Unable to determine executable path");
#endif
    }
}

int main(int argc, char *argv[])
{
    setupTerminateLogger();

    try
    {
        std::filesystem::path exePath = getExecutablePath(argc, argv);
    
        std::string elfPath = exePath.filename().string();
        std::filesystem::path pathObj(elfPath);
        std::string folderName = pathObj.filename().string();
        std::string normalizedId = normalizeGameId(folderName);

        std::string windowTitle = "PS2-Recomp | ";
        const char *gameName = getGameName(normalizedId);

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
}