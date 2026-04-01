#include "Common.h"
#include "Unimplemented.h"

namespace ps2_stubs
{
    void TODO(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("unknown", rdram, ctx, runtime);
    }


    void TODO_NAMED(const char *name, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const std::string stubName = name ? name : "unknown";

        uint32_t callCount = 0;
        {
            std::lock_guard<std::mutex> lock(g_stubWarningMutex);
            callCount = ++g_stubWarningCount[stubName];
        }

        if (callCount > kMaxStubWarningsPerName)
        {
            if (callCount == (kMaxStubWarningsPerName + 1))
            {
                std::cerr << "Warning: Further calls to PS2 stub '" << stubName
                          << "' are suppressed after " << kMaxStubWarningsPerName << " warnings" << std::endl;
            }
            setReturnS32(ctx, -1);
            return;
        }

        uint32_t stub_num = getRegU32(ctx, 2);   // $v0
        uint32_t caller_ra = getRegU32(ctx, 31); // $ra

        std::cerr << "Warning: Unimplemented PS2 stub called. name=" << stubName
                  << " PC=0x" << std::hex << ctx->pc
                  << ", RA=0x" << caller_ra
                  << ", Stub# guess (from $v0)=0x" << stub_num << std::dec << std::endl;

        // More context for debugging
        std::cerr << "  Args: $a0=0x" << std::hex << getRegU32(ctx, 4)
                  << ", $a1=0x" << getRegU32(ctx, 5)
                  << ", $a2=0x" << getRegU32(ctx, 6)
                  << ", $a3=0x" << getRegU32(ctx, 7) << std::dec << std::endl;

        setReturnS32(ctx, -1); // Return error
    }
}
