#ifndef PS2RECOMP_FUNCTION_CLASSIFIER_H
#define PS2RECOMP_FUNCTION_CLASSIFIER_H

#include <cstdint>
#include <string>
#include <unordered_set>

namespace ps2recomp
{
    class FunctionClassifier
    {
    public:
        FunctionClassifier();

        void setSceSdkFunctionNames(const std::unordered_set<std::string> *names);
        bool isLibraryFunction(const std::string &name) const;
        static bool hasRuntimeHandler(const std::string &name);

        static bool isReliableSymbolName(const std::string &name);
        static bool isSystemSymbolName(const std::string &name);
        static bool shouldAutoSkipName(const std::string &name);
        static bool shouldSkipSystemSymbol(const std::string &name, const std::unordered_set<std::string> &forcedRecompileNames);
        static bool hasPs2ApiPrefix(const std::string &name);
        static bool isDoNotSkipOrStub(const std::string &name);
        static bool isKnownLocalHelperName(const std::string &name);

    private:
        std::unordered_set<std::string> m_knownLibNames;
        const std::unordered_set<std::string> *m_sceSdkFunctionNames = nullptr;

        void initializeKnownLibraryFunctions();
        static bool matchesKernelRuntimeName(const std::string &name);
    };
}

#endif // PS2RECOMP_FUNCTION_CLASSIFIER_H
