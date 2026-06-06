#include "ps2recomp/function_classifier.h"

#include "ps2_runtime_calls.h"

#include <cctype>
#include <regex>
#include <vector>

namespace ps2recomp
{
    FunctionClassifier::FunctionClassifier()
    {
        initializeKnownLibraryFunctions();
    }

    void FunctionClassifier::setSceSdkFunctionNames(const std::unordered_set<std::string> *names)
    {
        m_sceSdkFunctionNames = names;
    }

    bool FunctionClassifier::hasRuntimeHandler(const std::string &name)
    {
        return !ps2_runtime_calls::resolveSyscallName(name).empty() ||
               !ps2_runtime_calls::resolveStubName(name).empty();
    }

    void FunctionClassifier::initializeKnownLibraryFunctions()
    {
        const std::vector<std::string> stdLibFuncs = {
            "printf", "sprintf", "snprintf", "fprintf", "vprintf", "vfprintf", "vsprintf", "vsnprintf",
            "puts", "putchar", "getchar", "gets", "fgets", "fputs", "scanf", "fscanf", "sscanf",
            "sprint", "sbprintf",
            "malloc", "free", "calloc", "realloc", "aligned_alloc", "posix_memalign",
            "memcpy", "memset", "memmove", "memcmp", "memchr", "bcopy", "bzero",
            "strcpy", "strncpy", "strcat", "strncat", "strcmp", "strncmp", "strlen", "strstr",
            "strchr", "strrchr", "strdup", "strtok", "strtok_r", "strerror",
            "fopen", "fclose", "fread", "fwrite", "fseek", "ftell", "rewind", "fflush",
            "fgetc", "fgets", "feof", "ferror", "clearerr", "fileno", "tmpfile", "remove", "rename",
            "open", "close", "read", "write", "lseek", "stat", "fstat",
            "atoi", "atol", "atoll", "atof", "strtol", "strtoul", "strtoll", "strtoull", "strtod", "strtof",
            "rand", "srand", "random", "srandom", "drand48", "sqrt", "pow", "exp", "log", "log10",
            "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh",
            "floor", "ceil", "fabs", "fmod", "frexp", "ldexp", "modf",
            "time", "ctime", "clock", "difftime", "mktime", "localtime", "gmtime", "asctime", "strftime",
            "gettimeofday", "nanosleep", "usleep",
            "abort", "exit", "_exit", "atexit", "system", "getpid", "fork", "waitpid",
            "qsort", "bsearch", "abs", "div", "labs", "ldiv", "llabs", "lldiv",
            "isalnum", "isalpha", "isdigit", "islower", "isupper", "isspace", "tolower", "toupper",
            "setjmp", "longjmp", "getenv", "setenv", "unsetenv",
            "perror", "fputc", "getc", "ungetc", "freopen", "setvbuf", "setbuf",
            "strnlen", "strspn", "strcspn", "strcasecmp", "strncasecmp"};

        m_knownLibNames.insert(stdLibFuncs.begin(), stdLibFuncs.end());
    }

    bool FunctionClassifier::hasPs2ApiPrefix(const std::string &name)
    {
        if (name.empty())
        {
            return false;
        }

        const std::vector<std::string> libraryPrefixes = {
            "sce", "Sce", "SCE",
            "sif", "Sif", "SIF",
            "gs", "Gs", "GS",
            "dma", "Dma", "DMA",
            "iop", "Iop", "IOP",
            "vif", "Vif", "VIF",
            "spu", "Spu", "SPU",
            "mc", "Mc", "MC",
            "libc", "Libc", "LIBC"};

        std::string base = name;
        if (base[0] == '_' && base.size() > 1)
        {
            base = base.substr(1);
        }

        auto hasSdkPrefixShape = [](const std::string &value, const std::string &prefix) -> bool
        {
            if (value.rfind(prefix, 0) != 0)
            {
                return false;
            }

            if (value.size() == prefix.size())
            {
                return true;
            }

            return !std::islower(static_cast<unsigned char>(value[prefix.size()]));
        };

        for (const auto &prefix : libraryPrefixes)
        {
            if (hasSdkPrefixShape(base, prefix))
            {
                return true;
            }
        }

        return false;
    }

    bool FunctionClassifier::matchesKernelRuntimeName(const std::string &name)
    {
        if (name.empty())
        {
            return false;
        }

        static const std::regex kernelRuntimePattern(
            "^(?:(?:Create|Delete|Start|ExitDelete|Exit|Terminate|Suspend|Resume|Sleep|Wakeup|CancelWakeup|Change|Rotate|Release|Setup|Register|Query|Get|Set|Refer|Poll|Wait|Signal|Enable|Disable|Flush|Reset|Add|Init)(?:Thread|Sema|EventFlag|Alarm|Intc|IntcHandler2|Dmac|DmacHandler2|OsdConfigParam|MemorySize|VSyncFlag|Heap|TLS|Status|Cache|Syscall|TLB|TLBEntry|GsCrt)|EndOfHeap|GsGetIMR|GsPutIMR|Deci2Call|Sif[A-Za-z0-9_]+|i(?:SignalSema|PollSema|ReferSemaStatus|SetEventFlag|ClearEventFlag|PollEventFlag|ReferEventFlagStatus|WakeupThread|CancelWakeupThread|ReleaseWaitThread|SetAlarm|CancelAlarm|FlushCache|sceSifSetDma|sceSifSetDChain))$");
        return std::regex_match(name, kernelRuntimePattern);
    }

    bool FunctionClassifier::isDoNotSkipOrStub(const std::string &name)
    {
        static const std::unordered_set<std::string> kDoNotSkipOrStub = {
            "topThread",
            "cmd_sem_init"};

        return kDoNotSkipOrStub.contains(name);
    }

    bool FunctionClassifier::isKnownLocalHelperName(const std::string &name)
    {
        static const std::unordered_set<std::string> kKnownLocalHelpers = {
            "memcpy2",
            "_memcpy2"};

        return kKnownLocalHelpers.contains(name);
    }

    bool FunctionClassifier::isReliableSymbolName(const std::string &name)
    {
        if (name.empty())
        {
            return false;
        }

        auto startsWith = [&](const char *prefix) -> bool
        {
            return name.rfind(prefix, 0) == 0;
        };

        if (startsWith("sub_") || startsWith("FUN_") || startsWith("func_") ||
            startsWith("entry_") || startsWith("function_") || startsWith("LAB_"))
        {
            return false;
        }

        bool hasAlpha = false;
        bool allHexOrPrefix = true;
        for (char c : name)
        {
            if (std::isalpha(static_cast<unsigned char>(c)))
            {
                hasAlpha = true;
            }

            if (!(std::isxdigit(static_cast<unsigned char>(c)) || c == 'x' || c == 'X' || c == '_'))
            {
                allHexOrPrefix = false;
            }
        }

        if (!hasAlpha)
        {
            return false;
        }

        if ((startsWith("0x") || startsWith("0X")) && allHexOrPrefix)
        {
            return false;
        }

        return true;
    }

    bool FunctionClassifier::isSystemSymbolName(const std::string &name)
    {
        if (!isReliableSymbolName(name))
        {
            return false;
        }

        static const std::unordered_set<std::string> systemFuncs = {
            "entry", "_start", "_init", "_fini",
            "abort", "exit", "_exit",
            "_profiler_start", "_profiler_stop",
            "__main", "__do_global_ctors", "__do_global_dtors",
            "_GLOBAL__sub_I_", "_GLOBAL__sub_D_",
            "__ctor_list", "__dtor_list", "_edata", "_end",
            "etext", "__exidx_start", "__exidx_end",
            "_ftext", "__bss_start", "__bss_start__",
            "__bss_end__", "__end__", "_stack", "_dso_handle"};

        return systemFuncs.contains(name) ||
               name.find("__") == 0 ||
               name.find(".") == 0;
    }

    bool FunctionClassifier::shouldAutoSkipName(const std::string &name)
    {
        if (isDoNotSkipOrStub(name))
        {
            return false;
        }

        if (!isReliableSymbolName(name))
        {
            return true;
        }

        return isSystemSymbolName(name);
    }

    bool FunctionClassifier::shouldSkipSystemSymbol(
        const std::string &name,
        const std::unordered_set<std::string> &forcedRecompileNames)
    {
        if (forcedRecompileNames.contains(name))
        {
            return false;
        }
        return isSystemSymbolName(name);
    }

    bool FunctionClassifier::isLibraryFunction(const std::string &name) const
    {
        if (name.empty())
        {
            return false;
        }

        if (!isReliableSymbolName(name))
        {
            return false;
        }

        if (isKnownLocalHelperName(name))
        {
            return false;
        }

        std::string normalizedName = name;
        if (normalizedName[0] == '_' && normalizedName.size() > 1)
        {
            normalizedName = normalizedName.substr(1);
        }

        if (isKnownLocalHelperName(normalizedName))
        {
            return false;
        }

        if (m_sceSdkFunctionNames != nullptr &&
            (m_sceSdkFunctionNames->contains(name) ||
             m_sceSdkFunctionNames->contains(normalizedName)))
        {
            return true;
        }

        if (matchesKernelRuntimeName(normalizedName))
        {
            return true;
        }

        if (m_knownLibNames.contains(name) ||
            m_knownLibNames.contains(normalizedName))
        {
            return true;
        }

        if (hasPs2ApiPrefix(name))
        {
            return true;
        }

        static const std::regex cLibPattern("^_*(mem|str|time|f?printf|f?scanf|malloc|free|calloc|realloc|atoi|itoa|rand|srand|abort|exit|atexit|getenv|system|bsearch|qsort|abs|labs|div|ldiv|mblen|mbtowc|wctomb|mbstowcs|wcstombs).*");
        return std::regex_match(normalizedName, cLibPattern);
    }
}
