#include "MiniTest.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/r5900_decoder.h"
#include "ps2recomp/types.h"
#include "ps2_runtime.h"
#include "runtime/ps2_memory.h"
#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ee_scheduler.h"
#include "runtime/gs/ps2_gs_psmct32.h"
#include "ps2_runtime_macros.h"
#include "Stubs/MPEG.h"
#include "Stubs/CD.h"
#include "Stubs/Audio.h"
#include "Stubs/GS.h"
#include "Stubs/VU.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

using namespace ps2recomp;
using namespace ps2_syscalls;

namespace
{
    constexpr uint32_t COP0_CAUSE_BD = 0x80000000u;
    constexpr uint32_t COP0_CAUSE_EXCCODE_MASK = 0x0000007Cu;
    constexpr uint32_t COP0_STATUS_EXL = 0x00000002u;
    constexpr uint32_t COP0_STATUS_BEV = 0x00400000u;
    constexpr uint32_t EXCEPTION_VECTOR_GENERAL = 0x80000080u;
    constexpr uint32_t EXCEPTION_VECTOR_BOOT = 0xBFC00200u;

    constexpr int KE_OK = 0;

    void setRegU32(R5900Context &ctx, int reg, uint32_t value)
    {
        ctx.r[reg] = _mm_set_epi64x(0, static_cast<int64_t>(value));
    }

    int32_t getRegS32(const R5900Context &ctx, int reg)
    {
        return static_cast<int32_t>(::getRegU32(&ctx, reg));
    }

    uint32_t makeVifCmd(uint8_t opcode, uint8_t num, uint16_t imm)
    {
        return (static_cast<uint32_t>(opcode) << 24) |
               (static_cast<uint32_t>(num) << 16) |
               static_cast<uint32_t>(imm);
    }

    uint32_t makeVuLq(uint8_t dest, uint8_t targetVf, uint8_t baseVi, int16_t imm)
    {
        return (static_cast<uint32_t>(dest & 0xFu) << 21) |
               (static_cast<uint32_t>(targetVf & 0x1Fu) << 16) |
               (static_cast<uint32_t>(baseVi & 0x1Fu) << 11) |
               (static_cast<uint32_t>(imm) & 0x7FFu);
    }

    uint32_t makeVuSq(uint8_t dest, uint8_t sourceVf, uint8_t baseVi, int16_t imm)
    {
        return (0x01u << 25) |
               (static_cast<uint32_t>(dest & 0xFu) << 21) |
               (static_cast<uint32_t>(baseVi & 0x1Fu) << 16) |
               (static_cast<uint32_t>(sourceVf & 0x1Fu) << 11) |
               (static_cast<uint32_t>(imm) & 0x7FFu);
    }

    uint32_t makeVuAdd(uint8_t dest, uint8_t fd, uint8_t fs, uint8_t ft)
    {
        return (static_cast<uint32_t>(dest & 0xFu) << 21) |
               (static_cast<uint32_t>(ft & 0x1Fu) << 16) |
               (static_cast<uint32_t>(fs & 0x1Fu) << 11) |
               (static_cast<uint32_t>(fd & 0x1Fu) << 6) |
               0x28u;
    }

    uint32_t makeVuIaddiu(uint8_t it, uint8_t is, int16_t immediate)
    {
        return (0x08u << 25) |
               (static_cast<uint32_t>(it & 0xFu) << 16) |
               (static_cast<uint32_t>(is & 0xFu) << 11) |
               (static_cast<uint32_t>(immediate) & 0x7FFu);
    }

    uint32_t makeVuLowerSpecial(uint8_t specialOp, uint8_t is,
                                uint8_t it = 0u, uint8_t dest = 0u)
    {
        return (0x40u << 25) |
               (static_cast<uint32_t>(dest & 0xFu) << 21) |
               (static_cast<uint32_t>(it & 0x1Fu) << 16) |
               (static_cast<uint32_t>(is & 0x1Fu) << 11) |
               (static_cast<uint32_t>(specialOp & 0x7Cu) << 4) |
               static_cast<uint32_t>(specialOp & 0x3u) |
               0x3Cu;
    }

    void writeVuInstructionPair(uint8_t *code, uint32_t pc, uint32_t lower, uint32_t upper)
    {
        std::memcpy(code + pc, &lower, sizeof(lower));
        std::memcpy(code + pc + sizeof(lower), &upper, sizeof(upper));
    }

    uint64_t packVuInstructionPair(uint32_t lower, uint32_t upper)
    {
        return static_cast<uint64_t>(lower) |
               (static_cast<uint64_t>(upper) << 32);
    }

    bool hasSignedRdWrite(const std::string &generated, uint8_t rd)
    {
        if (rd == 0u)
        {
            return false;
        }

        const std::string needle = "SET_GPR_S32(ctx, " + std::to_string(rd) + ",";
        return generated.find(needle) != std::string::npos;
    }

    uint32_t frameOffsetBytes(uint32_t x, uint32_t y, uint32_t fbw)
    {
        return GSPSMCT32::addrPSMCT32(0u, (fbw != 0u) ? fbw : 1u, x, y);
    }

    void testResumeOwnerFallbackHandler(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        if (ctx)
        {
            setRegU32(*ctx, 2, 0x00ABC123u);
            ctx->pc = 0u;
        }
    }

    void testResumeNextFunctionHandler(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        if (ctx)
        {
            setRegU32(*ctx, 2, 0x00555555u);
            ctx->pc = 0u;
        }
    }

    void testGuestBranchImplicitReturnHandler(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        if (ctx)
        {
            setRegU32(*ctx, 2, 0x00FACE42u);
            // Leave ctx->pc at the entry point. dispatchGuestBranch should convert
            // unchanged call PC into the supplied fallthrough PC for call-like edges.
        }
    }

    void testGuestBranchTransferHandler(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        if (ctx)
        {
            setRegU32(*ctx, 2, 0x00BEEFu);
            ctx->pc = 0x33330000u;
        }
    }

    std::atomic<uint32_t> gGuestJumpTargetCount{0u};

    void testGuestJumpTargetHandler(uint8_t *, R5900Context *, PS2Runtime *)
    {
        gGuestJumpTargetCount.fetch_add(1u, std::memory_order_relaxed);
    }

    std::atomic<uint32_t> gMpegStreamCallbackCount{0u};
    std::atomic<uint32_t> gMpegStreamCallbackMpeg{0u};
    std::atomic<uint32_t> gMpegStreamCallbackType{0u};
    std::atomic<uint32_t> gMpegStreamCallbackDataAddr{0u};
    std::atomic<uint32_t> gMpegStreamCallbackLen{0u};
    std::atomic<uint32_t> gMpegStreamCallbackUserData{0u};
    constexpr uint32_t kMpegCallbackStopPc = 0x00124FF0u;
    std::atomic<int32_t> gMpegWaitResult{-999};
    std::atomic<uint32_t> gMpegWaitStage{0u};
    std::atomic<uint32_t> gMpegNoDuplicateStage{0u};
    std::atomic<uint32_t> gMpegNoDuplicateProducerStage{0u};

    constexpr uint32_t kMpegWaitMainPc = 0x00125000u;
    constexpr uint32_t kMpegWaitResumePc = 0x00125010u;
    constexpr uint32_t kMpegWaitProducerPc = 0x00125020u;
    constexpr uint32_t kMpegWaitHandle = 0x00123000u;
    constexpr uint32_t kMpegWaitImage = 0x00130000u;
    constexpr uint32_t kMpegNoDuplicateMainPc = 0x00125030u;
    constexpr uint32_t kMpegNoDuplicateResumePc = 0x00125040u;
    constexpr uint32_t kMpegNoDuplicateProducerPc = 0x00125050u;
    constexpr uint32_t kMpegNoDuplicateHandle = 0x00124000u;
    constexpr uint32_t kMpegNoDuplicateImage = 0x00131000u;
    constexpr uint32_t kIpuInitMainPc = 0x00125100u;
    constexpr uint32_t kIpuInitResumePc = 0x00125104u;
    constexpr uint32_t kIpuSetD4Pc = 0x00126428u;
    std::atomic<uint32_t> gIpuSetD4Hits{0u};
    std::atomic<uint32_t> gIpuSetD4Argument{0u};
    std::atomic<int32_t> gIpuInitResult{-999};

    void testIpuSetD4(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        gIpuSetD4Hits.fetch_add(1u, std::memory_order_acq_rel);
        gIpuSetD4Argument.store(::getRegU32(ctx, 4), std::memory_order_release);
        ctx->pc = 0u;
    }

    void testIpuInitMain(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ctx->pc = kIpuInitResumePc;
        ps2_stubs::sceIpuInit(rdram, ctx, runtime);
    }

    void testIpuInitResume(uint8_t *, R5900Context *ctx, PS2Runtime *runtime)
    {
        gIpuInitResult.store(getRegS32(*ctx, 2), std::memory_order_release);
        ctx->pc = 0u;
        runtime->requestStop();
    }

    void testMpegWaitMain(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        gMpegWaitStage.store(1u, std::memory_order_release);
        setRegU32(*ctx, 4, kMpegWaitHandle);
        setRegU32(*ctx, 5, kMpegWaitImage);
        ctx->pc = kMpegWaitResumePc;
        ps2_stubs::sceMpegGetPicture(rdram, ctx, runtime);
    }

    void testMpegWaitResume(uint8_t *, R5900Context *ctx, PS2Runtime *runtime)
    {
        gMpegWaitStage.store(3u, std::memory_order_release);
        gMpegWaitResult.store(static_cast<int32_t>(::getRegU32(ctx, 2)), std::memory_order_release);
        ctx->pc = 0u;
        runtime->requestStop();
    }

    void testStopAfterMpegCallback(uint8_t *, R5900Context *ctx, PS2Runtime *runtime)
    {
        ctx->pc = 0u;
        runtime->requestStop();
    }

    void testMpegWaitProducer(uint8_t *, R5900Context *ctx, PS2Runtime *runtime)
    {
        gMpegWaitStage.store(2u, std::memory_order_release);
        ps2_stubs::notifyMpegCdStreamEof(runtime);
        ctx->pc = 0u;
    }

    void testMpegNoDuplicateMain(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setRegU32(*ctx, 4, kMpegNoDuplicateHandle);
        setRegU32(*ctx, 5, kMpegNoDuplicateImage);
        ps2_stubs::sceMpegGetPicture(rdram, ctx, runtime);

        gMpegNoDuplicateStage.store(1u, std::memory_order_release);
        ctx->pc = kMpegNoDuplicateResumePc;
        ps2_stubs::sceMpegGetPicture(rdram, ctx, runtime);

        gMpegNoDuplicateStage.store(4u, std::memory_order_release);
        ctx->pc = 0u;
        runtime->requestStop();
    }

    void testMpegNoDuplicateResume(uint8_t *, R5900Context *ctx, PS2Runtime *runtime)
    {
        gMpegNoDuplicateStage.store(3u, std::memory_order_release);
        ctx->pc = 0u;
        runtime->requestStop();
    }

    void testMpegNoDuplicateProducer(uint8_t *, R5900Context *ctx, PS2Runtime *runtime)
    {
        gMpegNoDuplicateProducerStage.store(
            gMpegNoDuplicateStage.load(std::memory_order_acquire),
            std::memory_order_release);
        gMpegNoDuplicateStage.store(2u, std::memory_order_release);
        ps2_stubs::notifyMpegCdStreamEof(runtime);
        ctx->pc = 0u;
    }

    void testRecordMpegStreamCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (!rdram || !ctx)
        {
            return;
        }

        const uint32_t cbData = ::getRegU32(ctx, 5);
        uint32_t type = 0u;
        uint32_t dataAddr = 0u;
        uint32_t len = 0u;
        std::memcpy(&type, rdram + cbData + 0x00u, sizeof(type));
        std::memcpy(&dataAddr, rdram + cbData + 0x08u, sizeof(dataAddr));
        std::memcpy(&len, rdram + cbData + 0x0Cu, sizeof(len));

        gMpegStreamCallbackMpeg.store(::getRegU32(ctx, 4), std::memory_order_release);
        gMpegStreamCallbackType.store(type, std::memory_order_release);
        gMpegStreamCallbackDataAddr.store(dataAddr, std::memory_order_release);
        gMpegStreamCallbackLen.store(len, std::memory_order_release);
        gMpegStreamCallbackUserData.store(::getRegU32(ctx, 6), std::memory_order_release);
        gMpegStreamCallbackCount.fetch_add(1u, std::memory_order_acq_rel);
        ctx->pc = 0u;
        runtime->requestStop();
    }

}

void register_ps2_runtime_expansion_tests()
{
    MiniTest::Case("PS2RuntimeExpansion", [](TestCase &tc)
    {
        tc.Run("differential decoder/codegen gpr-write contract for MULT and DIV families", [](TestCase &t)
        {
            R5900Decoder decoder;
            CodeGenerator generator({}, {});

            const struct
            {
                const char *name;
                uint32_t raw;
            } cases[] = {
                {"MULT rd!=0", (OPCODE_SPECIAL << 26) | (4u << 21) | (5u << 16) | (3u << 11) | SPECIAL_MULT},
                {"MULT rd==0", (OPCODE_SPECIAL << 26) | (4u << 21) | (5u << 16) | (0u << 11) | SPECIAL_MULT},
                {"DIV rd!=0", (OPCODE_SPECIAL << 26) | (6u << 21) | (7u << 16) | (9u << 11) | SPECIAL_DIV},
                {"MMI MULT1 rd!=0", (OPCODE_MMI << 26) | (8u << 21) | (9u << 16) | (10u << 11) | MMI_MULT1},
                {"MMI DIV1 rd!=0", (OPCODE_MMI << 26) | (8u << 21) | (9u << 16) | (10u << 11) | MMI_DIV1},
            };

            for (size_t i = 0; i < std::size(cases); ++i)
            {
                const Instruction inst = decoder.decodeInstruction(0x1000u + static_cast<uint32_t>(i * 4u), cases[i].raw);
                const std::string generated = generator.translateInstruction(inst);
                const bool emittedRdWrite = hasSignedRdWrite(generated, inst.rd);

                t.Equals(emittedRdWrite, inst.modificationInfo.modifiesGPR,
                         std::string("decoder/codegen mismatch for ") + cases[i].name);
                t.IsTrue(inst.modificationInfo.modifiesControl,
                         std::string("HI/LO control side-effect missing for ") + cases[i].name);
            }
        });

        tc.Run("lookupFunction rejects internal resume PCs without exact registration", [](TestCase &t)
        {
            PS2Runtime runtime;
            runtime.setMissingFunctionPolicy(PS2Runtime::MissingFunctionPolicy::Stop);
            runtime.registerFunction(0x1000u, &testResumeOwnerFallbackHandler);
            runtime.registerFunction(0x1100u, &testResumeNextFunctionHandler);

            R5900Context ctx{};
            ctx.pc = 0x1010u;
            auto fn = runtime.lookupFunction(ctx.pc);
            fn(nullptr, &ctx, &runtime);

            t.Equals(::getRegU32(&ctx, 2), 0u,
                     "unregistered resume PC should not alias to the nearest owner");
            t.IsTrue(runtime.isStopRequested(),
                     "missing exact dispatch target should request runtime stop");
        });

        tc.Run("lookupFunction rejects final-function PCs inside code regions without exact registration", [](TestCase &t)
        {
            PS2Runtime runtime;
            runtime.setMissingFunctionPolicy(PS2Runtime::MissingFunctionPolicy::Stop);
            runtime.memory().registerCodeRegion(0x2000u, 0x2100u);
            runtime.registerFunction(0x2000u, &testResumeOwnerFallbackHandler);

            R5900Context ctx{};
            ctx.pc = 0x2010u;
            auto fn = runtime.lookupFunction(ctx.pc);
            fn(nullptr, &ctx, &runtime);

            t.Equals(::getRegU32(&ctx, 2), 0u,
                     "code-region membership alone should not alias to the previous function");
            t.IsTrue(runtime.isStopRequested(),
                     "missing exact final-function target should request runtime stop");
        });

        tc.Run("dispatchGuestBranch call normalizes unchanged callee PC to fallthrough", [](TestCase &t)
        {
            PS2Runtime runtime;
            runtime.registerFunction(0x3000u, &testGuestBranchImplicitReturnHandler);

            R5900Context ctx{};
            ctx.pc = 0x2000u;

            const bool returnedToFallthrough = runtime.dispatchGuestBranch(
                nullptr,
                &ctx,
                0x3000u,
                0x2000u,
                0x2008u,
                PS2Runtime::GuestBranchKind::IndirectCall,
                "test-jalr");

            t.IsTrue(returnedToFallthrough,
                     "call-like dispatch should report true when it resumes at fallthrough");
            t.Equals(ctx.pc, 0x2008u,
                     "unchanged callee PC should be converted to call fallthrough");
            t.Equals(::getRegU32(&ctx, 2), 0x00FACE42u,
                     "callee should still execute normally");
        });

        tc.Run("dispatchGuestBranch jump returns to central dispatcher without nesting", [](TestCase &t)
        {
            PS2Runtime runtime;
            runtime.registerFunction(0x3400u, &testGuestJumpTargetHandler);
            gGuestJumpTargetCount.store(0u, std::memory_order_relaxed);

            R5900Context ctx{};
            ctx.pc = 0x2000u;

            const bool continuedInCaller = runtime.dispatchGuestBranch(
                nullptr,
                &ctx,
                0x3400u,
                0x2000u,
                0u,
                PS2Runtime::GuestBranchKind::IndirectJump,
                "test-jr");

            t.IsFalse(continuedInCaller,
                      "jump should stop the current generated wrapper");
            t.Equals(gGuestJumpTargetCount.load(std::memory_order_relaxed), 0u,
                     "jump target must not execute on a nested host stack frame");
            t.Equals(ctx.pc, 0x3400u,
                     "central dispatcher should receive the exact jump target");
        });

        tc.Run("dispatchGuestBranch call returns false when callee transfers elsewhere", [](TestCase &t)
        {
            PS2Runtime runtime;
            runtime.registerFunction(0x3100u, &testGuestBranchTransferHandler);

            R5900Context ctx{};
            ctx.pc = 0x2000u;

            const bool returnedToFallthrough = runtime.dispatchGuestBranch(
                nullptr,
                &ctx,
                0x3100u,
                0x2000u,
                0x2008u,
                PS2Runtime::GuestBranchKind::IndirectCall,
                "test-jalr-transfer");

            t.IsFalse(returnedToFallthrough,
                      "call-like dispatch should stop caller flow when callee transfers elsewhere");
            t.Equals(ctx.pc, 0x33330000u,
                     "callee transfer PC should be preserved");
        });

        tc.Run("dispatchGuestBranch rejects missing exact targets", [](TestCase &t)
        {
            PS2Runtime runtime;
            runtime.setMissingFunctionPolicy(PS2Runtime::MissingFunctionPolicy::Stop);
            runtime.registerFunction(0x3200u, &testGuestBranchImplicitReturnHandler);

            R5900Context ctx{};
            ctx.pc = 0x2000u;

            const bool returnedToFallthrough = runtime.dispatchGuestBranch(
                nullptr,
                &ctx,
                0x3210u,
                0x2000u,
                0x2008u,
                PS2Runtime::GuestBranchKind::IndirectCall,
                "test-missing");

            t.IsFalse(returnedToFallthrough,
                      "missing target should not resume caller flow");
            t.IsTrue(runtime.isStopRequested(),
                     "missing exact target should request runtime stop");
            t.Equals(ctx.pc, 0x3210u,
                     "missing target should remain visible in ctx->pc for diagnostics");
        });

        tc.Run("MPEG init and callback stubs return success instead of TODO errors", [](TestCase &t)
        {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            ps2_stubs::resetMpegStubState();

            R5900Context initCtx{};
            ps2_stubs::sceMpegInit(rdram.data(), &initCtx, nullptr);
            t.Equals(getRegS32(initCtx, 2), 0,
                     "sceMpegInit should succeed so games can continue through movie setup");

            R5900Context addCtx0{};
            setRegU32(addCtx0, 4, 0x00123000u);
            setRegU32(addCtx0, 5, 1u);
            setRegU32(addCtx0, 6, 0x00124000u);
            setRegU32(addCtx0, 7, 0u);
            ps2_stubs::sceMpegAddCallback(rdram.data(), &addCtx0, nullptr);
            t.Equals(getRegS32(addCtx0, 2), 1,
                     "first sceMpegAddCallback should hand back a non-error callback handle");

            R5900Context addCtx1{};
            setRegU32(addCtx1, 4, 0x00123000u);
            setRegU32(addCtx1, 5, 2u);
            setRegU32(addCtx1, 6, 0x00124010u);
            setRegU32(addCtx1, 7, 0u);
            ps2_stubs::sceMpegAddCallback(rdram.data(), &addCtx1, nullptr);
            t.Equals(getRegS32(addCtx1, 2), 2,
                     "subsequent sceMpegAddCallback calls should keep succeeding");

            R5900Context reinitCtx{};
            ps2_stubs::sceMpegInit(rdram.data(), &reinitCtx, nullptr);

            R5900Context addAfterReinit{};
            setRegU32(addAfterReinit, 4, 0x00123000u);
            setRegU32(addAfterReinit, 5, 3u);
            setRegU32(addAfterReinit, 6, 0x00124020u);
            setRegU32(addAfterReinit, 7, 0u);
            ps2_stubs::sceMpegAddCallback(rdram.data(), &addAfterReinit, nullptr);
            t.Equals(getRegS32(addAfterReinit, 2), 1,
                     "sceMpegInit should reset MPEG callback bookkeeping between runs");
        });

        tc.Run("sceMpegDemuxPssRing dispatches registered video and audio stream callbacks", [](TestCase &t)
        {
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            ps2_stubs::resetMpegStubState();

            constexpr uint32_t kMpegAddr = 0x00123000u;
            constexpr uint32_t kCallbackEntry = 0x00124000u;
            constexpr uint32_t kVideoUserData = 0x11223344u;
            constexpr uint32_t kAudioUserData = 0x55667788u;
            constexpr uint32_t kVideoPacketAddr = 0x00128000u;
            constexpr uint32_t kAudioPacketAddr = 0x00129000u;

            runtime.registerFunction(kCallbackEntry, &testRecordMpegStreamCallback);
            runtime.registerFunction(kMpegCallbackStopPc, &testStopAfterMpegCallback);
            auto prepareCallbackDispatch = [&]()
            {
                R5900Context idleContext{};
                idleContext.pc = kMpegCallbackStopPc;
                runtime.eeScheduler().reset(rdram.data(), idleContext);
            };

            auto registerGenericCallback = [&](uint32_t callbackType, uint32_t userData)
            {
                R5900Context addCtx{};
                setRegU32(addCtx, 4, kMpegAddr);
                setRegU32(addCtx, 5, callbackType);
                setRegU32(addCtx, 6, kCallbackEntry);
                setRegU32(addCtx, 7, userData);
                ps2_stubs::sceMpegAddCallback(rdram.data(), &addCtx, &runtime);
            };

            auto registerStreamCallback = [&](uint32_t streamType, uint32_t userData)
            {
                R5900Context addCtx{};
                setRegU32(addCtx, 4, kMpegAddr);
                setRegU32(addCtx, 5, streamType);
                setRegU32(addCtx, 6, 0u);
                setRegU32(addCtx, 7, kCallbackEntry);
                setRegU32(addCtx, 8, userData);
                ps2_stubs::sceMpegAddStrCallback(rdram.data(), &addCtx, &runtime);
            };

            auto writePesPacket = [&](uint32_t addr, uint8_t streamId, const std::vector<uint8_t> &payload)
            {
                const uint16_t packetLen = static_cast<uint16_t>(payload.size() + 3u);
                std::vector<uint8_t> packet = {
                    0x00u, 0x00u, 0x01u, streamId,
                    static_cast<uint8_t>(packetLen >> 8u),
                    static_cast<uint8_t>(packetLen & 0xFFu),
                    0x80u, 0x00u, 0x00u};
                packet.insert(packet.end(), payload.begin(), payload.end());
                std::memcpy(rdram.data() + addr, packet.data(), packet.size());
                return static_cast<uint32_t>(packet.size());
            };

            registerGenericCallback(0u, 0xDEAD0000u);
            registerGenericCallback(2u, 0xDEAD0002u);
            registerStreamCallback(0u, kVideoUserData);
            registerStreamCallback(2u, kAudioUserData);

            const std::vector<uint8_t> videoPayload = {
                0x00u, 0x00u, 0x01u, 0xB3u, 0x14u, 0x00u, 0xF0u, 0x13u};
            const uint32_t videoPacketSize = writePesPacket(kVideoPacketAddr, 0xE0u, videoPayload);

            gMpegStreamCallbackCount.store(0u, std::memory_order_release);
            prepareCallbackDispatch();
            R5900Context videoDemuxCtx{};
            setRegU32(videoDemuxCtx, 4, kMpegAddr);
            setRegU32(videoDemuxCtx, 5, kVideoPacketAddr);
            setRegU32(videoDemuxCtx, 6, videoPacketSize);
            setRegU32(videoDemuxCtx, 7, kVideoPacketAddr);
            setRegU32(videoDemuxCtx, 8, videoPacketSize);
            ps2_stubs::sceMpegDemuxPssRing(rdram.data(), &videoDemuxCtx, &runtime);
            runtime.eeScheduler().run();

            t.Equals(getRegS32(videoDemuxCtx, 2), static_cast<int32_t>(videoPacketSize),
                     "sceMpegDemuxPssRing should consume the video PES packet");
            t.Equals(gMpegStreamCallbackCount.load(std::memory_order_acquire), 1u,
                     "registered video stream callback should be invoked");
            t.Equals(gMpegStreamCallbackMpeg.load(std::memory_order_acquire), kMpegAddr,
                     "video callback should receive the MPEG handle");
            t.Equals(gMpegStreamCallbackType.load(std::memory_order_acquire), 0u,
                     "video callback data should report M2V stream type");
            t.Equals(gMpegStreamCallbackDataAddr.load(std::memory_order_acquire), kVideoPacketAddr + 9u,
                     "video callback data should point at PES payload");
            t.Equals(gMpegStreamCallbackLen.load(std::memory_order_acquire),
                     static_cast<uint32_t>(videoPayload.size()),
                     "video callback data should report PES payload length");
            t.Equals(gMpegStreamCallbackUserData.load(std::memory_order_acquire), kVideoUserData,
                     "video callback should receive registered user data");

            const std::vector<uint8_t> audioPayload = {0x80u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u};
            const uint32_t audioPacketSize = writePesPacket(kAudioPacketAddr, 0xBDu, audioPayload);

            gMpegStreamCallbackCount.store(0u, std::memory_order_release);
            prepareCallbackDispatch();
            R5900Context audioDemuxCtx{};
            setRegU32(audioDemuxCtx, 4, kMpegAddr);
            setRegU32(audioDemuxCtx, 5, kAudioPacketAddr);
            setRegU32(audioDemuxCtx, 6, audioPacketSize);
            setRegU32(audioDemuxCtx, 7, kAudioPacketAddr);
            setRegU32(audioDemuxCtx, 8, audioPacketSize);
            ps2_stubs::sceMpegDemuxPssRing(rdram.data(), &audioDemuxCtx, &runtime);
            runtime.eeScheduler().run();

            t.Equals(getRegS32(audioDemuxCtx, 2), static_cast<int32_t>(audioPacketSize),
                     "sceMpegDemuxPssRing should consume the audio PES packet");
            t.Equals(gMpegStreamCallbackCount.load(std::memory_order_acquire), 1u,
                     "registered audio stream callback should be invoked");
            t.Equals(gMpegStreamCallbackType.load(std::memory_order_acquire), 2u,
                     "audio callback data should report PCM stream type");
            t.Equals(gMpegStreamCallbackDataAddr.load(std::memory_order_acquire), kAudioPacketAddr + 9u,
                     "audio callback data should point at PES payload");
            t.Equals(gMpegStreamCallbackLen.load(std::memory_order_acquire),
                     static_cast<uint32_t>(audioPayload.size()),
                     "audio callback data should report PES payload length");
            t.Equals(gMpegStreamCallbackUserData.load(std::memory_order_acquire), kAudioUserData,
                     "audio callback should receive registered user data");

            ps2_stubs::notifyMpegCdStreamEof();

            gMpegStreamCallbackCount.store(0u, std::memory_order_release);
            R5900Context afterEofDemuxCtx{};
            setRegU32(afterEofDemuxCtx, 4, kMpegAddr);
            setRegU32(afterEofDemuxCtx, 5, kVideoPacketAddr);
            setRegU32(afterEofDemuxCtx, 6, videoPacketSize);
            setRegU32(afterEofDemuxCtx, 7, kVideoPacketAddr);
            setRegU32(afterEofDemuxCtx, 8, videoPacketSize);
            ps2_stubs::sceMpegDemuxPssRing(rdram.data(), &afterEofDemuxCtx, &runtime);

            t.Equals(getRegS32(afterEofDemuxCtx, 2), static_cast<int32_t>(videoPacketSize),
                     "post-EOF demux should continue consuming caller data");
            t.Equals(gMpegStreamCallbackCount.load(std::memory_order_acquire), 0u,
                     "post-EOF demux should not feed callbacks again");

            R5900Context resetCtx{};
            setRegU32(resetCtx, 4, kMpegAddr);
            ps2_stubs::sceMpegReset(rdram.data(), &resetCtx, &runtime);

            gMpegStreamCallbackCount.store(0u, std::memory_order_release);
            R5900Context afterResetDemuxCtx{};
            setRegU32(afterResetDemuxCtx, 4, kMpegAddr);
            setRegU32(afterResetDemuxCtx, 5, kVideoPacketAddr);
            setRegU32(afterResetDemuxCtx, 6, videoPacketSize);
            setRegU32(afterResetDemuxCtx, 7, kVideoPacketAddr);
            setRegU32(afterResetDemuxCtx, 8, videoPacketSize);
            ps2_stubs::sceMpegDemuxPssRing(rdram.data(), &afterResetDemuxCtx, &runtime);

            t.Equals(getRegS32(afterResetDemuxCtx, 2), static_cast<int32_t>(videoPacketSize),
                     "post-EOF reset demux should still drain caller data");
            t.Equals(gMpegStreamCallbackCount.load(std::memory_order_acquire), 0u,
                     "post-EOF reset demux should not restart callbacks on stale data");

            ps2_stubs::notifyMpegCdStreamStart();

            gMpegStreamCallbackCount.store(0u, std::memory_order_release);
            prepareCallbackDispatch();
            R5900Context afterNewStreamDemuxCtx{};
            setRegU32(afterNewStreamDemuxCtx, 4, kMpegAddr);
            setRegU32(afterNewStreamDemuxCtx, 5, kVideoPacketAddr);
            setRegU32(afterNewStreamDemuxCtx, 6, videoPacketSize);
            setRegU32(afterNewStreamDemuxCtx, 7, kVideoPacketAddr);
            setRegU32(afterNewStreamDemuxCtx, 8, videoPacketSize);
            ps2_stubs::sceMpegDemuxPssRing(rdram.data(), &afterNewStreamDemuxCtx, &runtime);
            runtime.eeScheduler().run();

            t.Equals(getRegS32(afterNewStreamDemuxCtx, 2), static_cast<int32_t>(videoPacketSize),
                     "new CD stream demux should reopen an ended MPEG handle");
            t.Equals(gMpegStreamCallbackCount.load(std::memory_order_acquire), 1u,
                     "new CD stream demux should allow callbacks on a reused MPEG handle");

            constexpr uint32_t kMpegWorkAddr = 0x00130000u;
            R5900Context createCtx{};
            setRegU32(createCtx, 4, kMpegAddr);
            setRegU32(createCtx, 5, kMpegWorkAddr);
            setRegU32(createCtx, 6, 0x2000u);
            ps2_stubs::sceMpegCreate(rdram.data(), &createCtx, &runtime);
            t.IsTrue(::getRegU32(&createCtx, 2) != 0u,
                     "sceMpegCreate should reopen the MPEG handle after an ended reset");

            gMpegStreamCallbackCount.store(0u, std::memory_order_release);
            prepareCallbackDispatch();
            R5900Context afterCreateDemuxCtx{};
            setRegU32(afterCreateDemuxCtx, 4, kMpegAddr);
            setRegU32(afterCreateDemuxCtx, 5, kVideoPacketAddr);
            setRegU32(afterCreateDemuxCtx, 6, videoPacketSize);
            setRegU32(afterCreateDemuxCtx, 7, kVideoPacketAddr);
            setRegU32(afterCreateDemuxCtx, 8, videoPacketSize);
            ps2_stubs::sceMpegDemuxPssRing(rdram.data(), &afterCreateDemuxCtx, &runtime);
            runtime.eeScheduler().run();

            t.Equals(gMpegStreamCallbackCount.load(std::memory_order_acquire), 1u,
                     "new MPEG create should allow callbacks for the next stream");

            runtime.requestStop();
        });

        tc.Run("sceMpegGetPicture blocks as a typed scheduler wait and resumes on EOF", [](TestCase &t)
        {
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            ps2_stubs::resetMpegStubState();
            ps2_stubs::notifyMpegCdStreamStart();
            runtime.registerFunction(kMpegWaitMainPc, testMpegWaitMain);
            runtime.registerFunction(kMpegWaitResumePc, testMpegWaitResume);
            runtime.registerFunction(kMpegWaitProducerPc, testMpegWaitProducer);
            gMpegWaitResult.store(-999, std::memory_order_release);
            gMpegWaitStage.store(0u, std::memory_order_release);

            R5900Context mainContext{};
            mainContext.pc = kMpegWaitMainPc;
            EeScheduler &ee = runtime.eeScheduler();
            ee.reset(rdram.data(), mainContext);
            const int producerId = ee.createThread(EeThreadCreateParams{
                0u, kMpegWaitProducerPc, 0u, 0u, 0u, 10, 0u});
            t.IsTrue(producerId > 1, "MPEG producer guest thread should be created");
            t.Equals(ee.startThread(producerId, 0u, mainContext, false), 0,
                     "MPEG producer guest thread should become ready");
            ee.run();

            t.Equals(gMpegWaitResult.load(std::memory_order_acquire), 0,
                     "EOF completion should resume GetPicture with success");
            t.Equals(gMpegWaitStage.load(std::memory_order_acquire), 3u,
                     "MPEG waiter should reach its continuation after the producer posts EOF");
            t.Equals(Ps2FastRead32(rdram.data(), kMpegWaitHandle + 0x00u), 320u,
                     "resumed GetPicture should publish the configured width");
            t.Equals(Ps2FastRead32(rdram.data(), kMpegWaitHandle + 0x04u), 240u,
                     "resumed GetPicture should publish the configured height");
        });

        tc.Run("sceMpegGetPicture waits for new decoder output instead of duplicating the last frame", [](TestCase &t)
        {
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            ps2_stubs::resetMpegStubState();
            ps2_stubs::notifyMpegCdStreamStart();
            ps2_stubs::enqueueMpegDecodedFrameForTesting(kMpegNoDuplicateHandle);
            runtime.registerFunction(kMpegNoDuplicateMainPc, testMpegNoDuplicateMain);
            runtime.registerFunction(kMpegNoDuplicateResumePc, testMpegNoDuplicateResume);
            runtime.registerFunction(kMpegNoDuplicateProducerPc, testMpegNoDuplicateProducer);
            gMpegNoDuplicateStage.store(0u, std::memory_order_release);
            gMpegNoDuplicateProducerStage.store(0u, std::memory_order_release);

            R5900Context mainContext{};
            mainContext.pc = kMpegNoDuplicateMainPc;
            EeScheduler &ee = runtime.eeScheduler();
            ee.reset(rdram.data(), mainContext);
            const int producerId = ee.createThread(EeThreadCreateParams{
                0u, kMpegNoDuplicateProducerPc, 0u, 0u, 0u, 10, 0u});
            t.IsTrue(producerId > 1, "MPEG producer guest thread should be created");
            t.Equals(ee.startThread(producerId, 0u, mainContext, false), 0,
                     "MPEG producer guest thread should become ready");
            ee.run();

            t.Equals(gMpegNoDuplicateProducerStage.load(std::memory_order_acquire), 1u,
                     "the producer should run while the second GetPicture is waiting");
            t.Equals(gMpegNoDuplicateStage.load(std::memory_order_acquire), 3u,
                     "EOF should resume the blocked GetPicture continuation");
            t.Equals(Ps2FastRead32(rdram.data(), kMpegNoDuplicateHandle + 0x08u), 1u,
                     "only the injected decoder frame should be counted as served");
        });

        tc.Run("sceSdRemote isolates voice transfers from block streaming state", [](TestCase &t)
        {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            constexpr uint32_t kStackAddr = 0x00100000u;
            constexpr uint32_t kBlockBase = 0x00012340u;
            constexpr uint32_t kBlockSize = 0x00003000u;
            constexpr uint32_t kBlockPause = 0x00012740u;

            R5900Context initCtx{};
            ps2_stubs::sceSdRemoteInit(rdram.data(), &initCtx, nullptr);

            R5900Context blockCtx{};
            setRegU32(blockCtx, 29, kStackAddr);
            setRegU32(blockCtx, 4, 1u);
            setRegU32(blockCtx, 5, 0x80E0u);
            setRegU32(blockCtx, 6, 1u);
            setRegU32(blockCtx, 7, 0x13u);
            setRegU32(blockCtx, 8, kBlockBase);
            setRegU32(blockCtx, 9, kBlockSize);
            setRegU32(blockCtx, 10, kBlockPause);
            ps2_stubs::sceSdRemote(rdram.data(), &blockCtx, nullptr);

            R5900Context blockStatusCtx{};
            setRegU32(blockStatusCtx, 29, kStackAddr);
            setRegU32(blockStatusCtx, 4, 1u);
            setRegU32(blockStatusCtx, 5, 0x8100u);
            setRegU32(blockStatusCtx, 6, 1u);
            setRegU32(blockStatusCtx, 7, 0u);
            ps2_stubs::sceSdRemote(rdram.data(), &blockStatusCtx, nullptr);
            t.Equals(getRegU32(&blockStatusCtx, 2), 0x00012B40u,
                     "initial block-status poll should advance the streaming ring");

            R5900Context voiceCtx{};
            setRegU32(voiceCtx, 29, kStackAddr);
            setRegU32(voiceCtx, 4, 1u);
            setRegU32(voiceCtx, 5, 0x80D0u);
            setRegU32(voiceCtx, 6, 0u);
            setRegU32(voiceCtx, 7, 0u);
            setRegU32(voiceCtx, 8, 0x00022000u);
            setRegU32(voiceCtx, 9, 0x00004000u);
            setRegU32(voiceCtx, 10, 0x00000800u);
            ps2_stubs::sceSdRemote(rdram.data(), &voiceCtx, nullptr);
            t.Equals(getRegU32(&voiceCtx, 2), 0x00000800u,
                     "DMA voice transfer should report its transferred byte count");

            R5900Context voiceStatusCtx{};
            setRegU32(voiceStatusCtx, 29, kStackAddr);
            setRegU32(voiceStatusCtx, 4, 1u);
            setRegU32(voiceStatusCtx, 5, 0x80F0u);
            setRegU32(voiceStatusCtx, 6, 0u);
            setRegU32(voiceStatusCtx, 7, 1u);
            ps2_stubs::sceSdRemote(rdram.data(), &voiceStatusCtx, nullptr);
            t.Equals(getRegU32(&voiceStatusCtx, 2), 1u,
                     "voice-transfer status should complete independently from block position");

            ps2_stubs::sceSdRemote(rdram.data(), &blockStatusCtx, nullptr);
            t.Equals(getRegU32(&blockStatusCtx, 2), 0x00012F40u,
                     "voice transfer should not replace or advance the block-streaming ring");
        });

        tc.Run("sceSdRemote keeps block cursors and loop banks isolated per core", [](TestCase &t)
        {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            constexpr uint32_t kStackAddr = 0x00100000u;

            R5900Context initCtx{};
            ps2_stubs::sceSdRemoteInit(rdram.data(), &initCtx, nullptr);

            auto remote = [&](uint32_t command,
                              uint32_t core,
                              uint32_t mode,
                              uint32_t arg4 = 0u,
                              uint32_t arg5 = 0u,
                              uint32_t arg6 = 0u)
            {
                R5900Context ctx{};
                setRegU32(ctx, 29, kStackAddr);
                setRegU32(ctx, 4, 1u);
                setRegU32(ctx, 5, command);
                setRegU32(ctx, 6, core);
                setRegU32(ctx, 7, mode);
                setRegU32(ctx, 8, arg4);
                setRegU32(ctx, 9, arg5);
                setRegU32(ctx, 10, arg6);
                ps2_stubs::sceSdRemote(rdram.data(), &ctx, nullptr);
                return getRegU32(&ctx, 2);
            };

            t.Equals(remote(0x80E0u, 0u, 0x10u, 0x00010000u, 0x00001000u, 0x00010000u), 0u,
                     "core 0 block stream should start successfully");
            t.Equals(remote(0x80E0u, 1u, 0x13u, 0x00020000u, 0x00002000u, 0x00020800u), 0u,
                     "core 1 block stream should start independently");

            t.Equals(remote(0x8100u, 0u, 0u), 0x00010400u,
                     "core 0 status should advance only the core 0 cursor");
            t.Equals(remote(0x8100u, 1u, 0u), 0x00020C00u,
                     "core 1 status should retain its independent pause position");
            t.Equals(remote(0x8100u, 0u, 0u), 0x01010800u,
                     "loop status should expose the second buffer in the high byte");

            t.Equals(remote(0x80E0u, 0u, 0x02u), 0x01010800u,
                     "block STOP should return the final core 0 cursor");
            t.Equals(remote(0x8100u, 0u, 0u), 0u,
                     "stopped block status should no longer expose a live cursor");
            t.Equals(remote(0x8100u, 1u, 0u), 0x01021000u,
                     "stopping core 0 should not stop or advance core 1");

            ps2_stubs::sceSdRemoteInit(rdram.data(), &initCtx, nullptr);
            t.Equals(remote(0x8100u, 1u, 0u), 0u,
                     "sceSdRemoteInit should reset block state for both cores");
            t.Equals(remote(0x80F0u, 1u, 0u), 1u,
                     "sceSdRemoteInit should restore idle voice status to complete");
        });

        tc.Run("IPU init skips missing optional helper instead of dispatching the default trap", [](TestCase &t)
        {
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            R5900Context ctx{};
            ctx.pc = 0x0010B470u;

            ps2_stubs::sceIpuInit(rdram.data(), &ctx, &runtime);

            t.IsFalse(runtime.isStopRequested(),
                      "sceIpuInit should tolerate the missing optional SetD4 helper");
            t.Equals(runtime.memory().read32(0x10002010u), 0x40000000u,
                     "sceIpuInit should still program IPU_CTRL");
            t.Equals(runtime.memory().read32(0x10002000u), 0u,
                     "sceIpuInit should leave IPU_CMD reset after initialization");
        });

        tc.Run("IPU init executes its guest helper as a scheduler invocation", [](TestCase &t)
        {
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            runtime.registerFunction(kIpuInitMainPc, testIpuInitMain);
            runtime.registerFunction(kIpuInitResumePc, testIpuInitResume);
            runtime.registerFunction(kIpuSetD4Pc, testIpuSetD4);
            gIpuSetD4Hits.store(0u, std::memory_order_release);
            gIpuSetD4Argument.store(0u, std::memory_order_release);
            gIpuInitResult.store(-999, std::memory_order_release);

            R5900Context context{};
            context.pc = kIpuInitMainPc;
            runtime.eeScheduler().reset(rdram.data(), context);
            runtime.eeScheduler().run();

            t.Equals(gIpuSetD4Hits.load(std::memory_order_acquire), 1u,
                     "the optional guest helper should execute exactly once through the dispatcher");
            t.Equals(gIpuSetD4Argument.load(std::memory_order_acquire), 1u,
                     "the invocation should receive the SetD4 enable argument");
            t.Equals(gIpuInitResult.load(std::memory_order_acquire), 0,
                     "the HLE completion should resume the preserved base context with success");
            t.Equals(runtime.memory().read32(0x10002010u), 0x40000000u,
                     "IPU initialization should finish only after the guest invocation completes");
        });

        tc.Run("sprintf consumes EE varargs from a2 a3 t0 and preserves width formatting", [](TestCase &t)
        {
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            R5900Context ctx{};

            constexpr uint32_t kDestAddr = 0x00002000u;
            constexpr uint32_t kFormatAddr = 0x00002100u;
            constexpr char kFormat[] = "rm_%1d%02d%1d.rdx";

            std::memcpy(rdram.data() + kFormatAddr, kFormat, sizeof(kFormat));
            setRegU32(ctx, 4, kDestAddr);
            setRegU32(ctx, 5, kFormatAddr);
            setRegU32(ctx, 6, 0u); // a2
            setRegU32(ctx, 7, 3u); // a3
            setRegU32(ctx, 8, 1u); // t0

            ps2_stubs::sprintf(rdram.data(), &ctx, &runtime);

            const std::string rendered(reinterpret_cast<const char *>(rdram.data() + kDestAddr));
            t.Equals(rendered, std::string("rm_0031.rdx"),
                     "sprintf should read the third variadic integer from t0 and honor %02d");
            t.Equals(getRegS32(ctx, 2), static_cast<int32_t>(rendered.size()),
                     "sprintf should return the rendered length");
        });

        tc.Run("multiply-add matrix writes rd only when R5900 requires it", [](TestCase &t)
        {
            R5900Decoder decoder;
            CodeGenerator generator({}, {});

            const struct
            {
                const char *name;
                uint32_t raw;
                bool expectedRdWrite;
            } cases[] = {
                {"MULTU rd!=0", (OPCODE_SPECIAL << 26) | (2u << 21) | (3u << 16) | (11u << 11) | SPECIAL_MULTU, true},
                {"MMI MADD rd!=0", (OPCODE_MMI << 26) | (2u << 21) | (3u << 16) | (12u << 11) | MMI_MADD, true},
                {"MMI MADDU rd!=0", (OPCODE_MMI << 26) | (2u << 21) | (3u << 16) | (13u << 11) | MMI_MADDU, true},
                {"MMI MADD1 rd!=0", (OPCODE_MMI << 26) | (2u << 21) | (3u << 16) | (14u << 11) | MMI_MADD1, true},
                {"MMI MADDU1 rd!=0", (OPCODE_MMI << 26) | (2u << 21) | (3u << 16) | (15u << 11) | MMI_MADDU1, true},
                {"MMI DIVU1 rd!=0", (OPCODE_MMI << 26) | (2u << 21) | (3u << 16) | (16u << 11) | MMI_DIVU1, false},
            };

            for (size_t i = 0; i < std::size(cases); ++i)
            {
                const Instruction inst = decoder.decodeInstruction(0x2000u + static_cast<uint32_t>(i * 4u), cases[i].raw);
                const std::string generated = generator.translateInstruction(inst);
                const bool emittedRdWrite = hasSignedRdWrite(generated, inst.rd);

                t.Equals(inst.modificationInfo.modifiesGPR, cases[i].expectedRdWrite,
                         std::string("decoder rd-write metadata mismatch for ") + cases[i].name);
                t.Equals(emittedRdWrite, cases[i].expectedRdWrite,
                         std::string("codegen rd-write mismatch for ") + cases[i].name);
            }
        });

        tc.Run("SignalException marks EPC and BD for delay-slot exceptions", [](TestCase &t)
        {
            PS2Runtime runtime;
            R5900Context ctx{};

            ctx.pc = 0x2000u;
            ctx.branch_pc = 0x1FFCu;
            ctx.in_delay_slot = true;
            ctx.cop0_status = 0u;
            ctx.cop0_cause = 0u;

            runtime.SignalException(&ctx, EXCEPTION_ADDRESS_ERROR_LOAD);

            t.Equals(ctx.cop0_epc, 0x1FFCu, "delay-slot exception should capture branch_pc in EPC");
            t.IsTrue((ctx.cop0_cause & COP0_CAUSE_BD) != 0u, "delay-slot exception should set CAUSE.BD");
            t.Equals(ctx.cop0_cause & COP0_CAUSE_EXCCODE_MASK,
                     (static_cast<uint32_t>(EXCEPTION_ADDRESS_ERROR_LOAD) << 2) & COP0_CAUSE_EXCCODE_MASK,
                     "CAUSE.EXCCODE should match exception");
            t.IsTrue((ctx.cop0_status & COP0_STATUS_EXL) != 0u, "exception should set STATUS.EXL");
            t.Equals(ctx.pc, EXCEPTION_VECTOR_GENERAL, "exception should jump to general vector when BEV=0");
            t.IsFalse(ctx.in_delay_slot, "exception delivery should clear delay-slot state");
        });

        tc.Run("SignalException uses current pc without BD and honors BEV vector", [](TestCase &t)
        {
            PS2Runtime runtime;
            R5900Context ctx{};

            ctx.pc = 0x3000u;
            ctx.in_delay_slot = false;
            ctx.cop0_status = COP0_STATUS_BEV;
            ctx.cop0_cause = COP0_CAUSE_BD;

            runtime.SignalException(&ctx, EXCEPTION_ADDRESS_ERROR_STORE);

            t.Equals(ctx.cop0_epc, 0x3000u, "non-delay exception should capture current pc in EPC");
            t.IsTrue((ctx.cop0_cause & COP0_CAUSE_BD) == 0u, "non-delay exception should clear CAUSE.BD");
            t.Equals(ctx.pc, EXCEPTION_VECTOR_BOOT, "BEV=1 should route exception to boot vector");
        });

        tc.Run("handleSyscall rejects invocation in delay slot", [](TestCase &t)
        {
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            R5900Context ctx{};
            ctx.in_delay_slot = true;

            bool threw = false;
            try
            {
                runtime.handleSyscall(rdram.data(), &ctx, 0x3Cu);
            }
            catch (const std::runtime_error &)
            {
                threw = true;
            }

            t.IsTrue(threw, "syscall from delay slot should throw to preserve block atomicity");
        });

        tc.Run("VIF MSCAL and MSCNT toggle DBF and keep TOPS/ITOPS coherent", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            mem.vif1_regs.base = 4u;
            mem.vif1_regs.ofst = 2u;
            mem.vif1_regs.tops = 4u;
            mem.vif1_regs.itops = 0x21u;
            mem.vif1_regs.stat &= ~(1u << 7); // DBF = 0

            uint32_t callbackPc = 0xFFFFFFFFu;
            uint32_t callbackTop = 0xFFFFFFFFu;
            uint32_t callbackItop = 0xFFFFFFFFu;
            uint32_t callbackCount = 0u;
            mem.setVu1MscalCallback([&](uint32_t startPC, uint32_t top, uint32_t itop)
            {
                callbackPc = startPC;
                callbackTop = top;
                callbackItop = itop;
                ++callbackCount;
            });

            const uint32_t mscal = makeVifCmd(0x14u, 0u, 3u); // start PC = 3 * 8
            mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&mscal), sizeof(mscal));

            t.Equals(callbackCount, 1u, "MSCAL should invoke VU1 callback exactly once");
            t.Equals(callbackPc, 24u, "MSCAL should pass startPC=imm*8");
            t.Equals(callbackTop, 4u, "MSCAL callback should receive current TOPS");
            t.Equals(callbackItop, 0x21u, "MSCAL callback should receive pending ITOPS");
            t.Equals(mem.vif1_regs.top, 4u, "MSCAL should latch TOP from TOPS");
            t.Equals(mem.vif1_regs.itop, 0x21u, "MSCAL should latch ITOP from ITOPS");
            t.IsTrue((mem.vif1_regs.stat & (1u << 7)) != 0u, "MSCAL should toggle DBF on");
            t.Equals(mem.vif1_regs.tops, 6u, "DBF=1 should make TOPS=BASE+OFST");

            const uint32_t mscnt = makeVifCmd(0x17u, 0u, 0u);
            mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&mscnt), sizeof(mscnt));

            t.Equals(callbackCount, 1u, "MSCNT should not invoke MSCAL callback");
            t.IsTrue((mem.vif1_regs.stat & (1u << 7)) == 0u, "MSCNT should toggle DBF back off");
            t.Equals(mem.vif1_regs.tops, 4u, "DBF=0 should make TOPS=BASE");
            t.Equals(mem.vif1_regs.top, 6u, "MSCNT should latch TOP from current TOPS before toggling");
            t.Equals(mem.vif1_regs.itop, 0x21u, "MSCNT should keep latching ITOP from ITOPS");
        });

        tc.Run("VU0 microprogram executes against VU0 code and data memory", [](TestCase &t)
        {
            PS2Runtime runtime;
            t.IsTrue(runtime.memory().initialize(), "PS2Memory initialize should succeed");
            t.IsTrue(runtime.syncCoreSubsystems(), "runtime core subsystems should bind");

            uint8_t *const code = runtime.memory().getVU0Code();
            uint8_t *const data = runtime.memory().getVU0Data();
            std::memset(code, 0, PS2_VU0_CODE_SIZE);
            std::memset(data, 0, PS2_VU0_DATA_SIZE);

            const float input[4] = {1.0f, 2.0f, 3.0f, 4.0f};
            std::memcpy(data, input, sizeof(input));

            constexpr uint32_t kVuNop = 0x0000003Fu;
            constexpr uint32_t kVuEndNop = 0x4000003Fu;
            writeVuInstructionPair(code, 0u, makeVuLq(0xFu, 1u, 0u, 0), kVuNop);
            writeVuInstructionPair(code, 8u, 0u, makeVuAdd(0xFu, 2u, 1u, 1u));
            writeVuInstructionPair(code, 16u, makeVuSq(0xFu, 2u, 0u, 1), kVuEndNop);

            R5900Context ctx{};
            runtime.executeVU0Microprogram(runtime.memory().getRDRAM(), &ctx, 0u);

            float output[4]{};
            std::memcpy(output, data + 16u, sizeof(output));
            t.Equals(output[0], 2.0f, "VU0 output x should be doubled");
            t.Equals(output[1], 4.0f, "VU0 output y should be doubled");
            t.Equals(output[2], 6.0f, "VU0 output z should be doubled");
            t.Equals(output[3], 8.0f, "VU0 output w should be doubled");

            alignas(16) float vf2[4]{};
            _mm_storeu_ps(vf2, ctx.vu0_vf[2]);
            t.Equals(vf2[0], 2.0f, "VU0 VF2.x should copy back to CPU context");
            t.Equals(static_cast<uint32_t>(ctx.vi[0]), 0u, "VU0 VI0 should remain zero");
        });

        tc.Run("VU0 microprogram preserves the architectural RNG state", [](TestCase &t)
        {
            PS2Runtime runtime;
            t.IsTrue(runtime.memory().initialize(), "PS2Memory initialize should succeed");
            t.IsTrue(runtime.syncCoreSubsystems(), "runtime core subsystems should bind");

            uint8_t *const code = runtime.memory().getVU0Code();
            std::memset(code, 0, PS2_VU0_CODE_SIZE);
            constexpr uint32_t kVuUpperNop = 0x000002FFu;
            constexpr uint32_t kVuUpperEndNop = 0x400002FFu;
            writeVuInstructionPair(
                code, 0u,
                makeVuLowerSpecial(0x40u, 0u, 1u, 0x8u),
                kVuUpperEndNop); // RNEXT.x vf1
            writeVuInstructionPair(code, 8u, 0u, kVuUpperNop);

            constexpr uint32_t seed = 0x3FC00000u;
            const uint32_t x = (seed >> 4) & 1u;
            const uint32_t y = (seed >> 22) & 1u;
            const uint32_t expected =
                (((seed << 1) ^ x ^ y) & 0x007FFFFFu) | 0x3F800000u;
            R5900Context ctx{};
            ctx.vu0_r = _mm_castsi128_ps(
                _mm_set1_epi32(static_cast<int32_t>(seed)));
            runtime.executeVU0Microprogram(runtime.memory().getRDRAM(), &ctx, 0u);

            alignas(16) uint32_t rWords[4]{};
            _mm_storeu_si128(reinterpret_cast<__m128i *>(rWords),
                             _mm_castps_si128(ctx.vu0_r));
            t.Equals(rWords[0], expected, "VU0 micro RNG should advance the imported R seed");
            t.Equals(rWords[1], expected, "VU0 R should remain replicated for macro-mode access");
            alignas(16) uint32_t vf1Words[4]{};
            _mm_storeu_si128(reinterpret_cast<__m128i *>(vf1Words),
                             _mm_castps_si128(ctx.vu0_vf[1]));
            t.Equals(vf1Words[0], expected, "RNEXT should expose the same R value through VF1.x");
        });

        tc.Run("VU0 direct MicroMem writes invalidate the fixed decode cache", [](TestCase &t)
        {
            PS2Runtime runtime;
            t.IsTrue(runtime.memory().initialize(), "PS2Memory initialize should succeed");
            t.IsTrue(runtime.syncCoreSubsystems(), "runtime core subsystems should bind");

            constexpr uint32_t kVuUpperNop = 0x000002FFu;
            constexpr uint32_t kVuUpperEndNop = 0x400002FFu;
            runtime.memory().write64(
                PS2_VU0_CODE_BASE,
                packVuInstructionPair(makeVuIaddiu(1u, 0u, 1), kVuUpperEndNop));
            runtime.memory().write64(
                PS2_VU0_CODE_BASE + 8u,
                packVuInstructionPair(0u, kVuUpperNop));

            R5900Context first{};
            runtime.executeVU0Microprogram(runtime.memory().getRDRAM(), &first, 0u);
            t.Equals(static_cast<uint32_t>(first.vi[1]), 1u,
                     "first cached VU0 microprogram should execute");

            runtime.memory().write64(
                PS2_VU0_CODE_BASE,
                packVuInstructionPair(makeVuIaddiu(1u, 0u, 2), kVuUpperEndNop));
            R5900Context second{};
            runtime.executeVU0Microprogram(runtime.memory().getRDRAM(), &second, 0u);
            t.Equals(static_cast<uint32_t>(second.vi[1]), 2u,
                     "VU0 cache should rebuild after a direct MicroMem write");
        });

        tc.Run("VU0 FBRST TE gates a T-bit microprogram stop", [](TestCase &t)
        {
            PS2Runtime runtime;
            t.IsTrue(runtime.memory().initialize(), "PS2Memory initialize should succeed");
            t.IsTrue(runtime.syncCoreSubsystems(), "runtime core subsystems should bind");

            uint8_t *const code = runtime.memory().getVU0Code();
            std::memset(code, 0, PS2_VU0_CODE_SIZE);
            constexpr uint32_t kVuUpperNop = 0x000002FFu;
            writeVuInstructionPair(
                code, 0u, makeVuIaddiu(1u, 0u, 7),
                kVuUpperNop | 0x08000000u);
            writeVuInstructionPair(
                code, 8u, makeVuIaddiu(2u, 0u, 9),
                kVuUpperNop);

            R5900Context ctx{};
            ctx.vu0_fbrst = 1u << 3; // TE0
            runtime.executeVU0Microprogram(runtime.memory().getRDRAM(), &ctx, 0u);

            t.Equals(static_cast<uint32_t>(ctx.vi[1]), 7u,
                     "the T-marked instruction should execute");
            t.Equals(static_cast<uint32_t>(ctx.vi[2]), 0u,
                     "TE0 should stop VU0 before the following instruction");
            t.IsTrue((ctx.vu0_vpu_stat & (1u << 2)) != 0u,
                     "VPU-STAT should report a VU0 T-bit stop");
            t.Equals(ctx.vu0_tpc, 8u,
                     "TPC should point at the first instruction not executed");
        });

        tc.Run("GS sprite draw applies XYOFFSET and fully-outside scissor should not render", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            const uint64_t frame1 =
                (0ull << 0) |   // FBP
                (1ull << 16) |  // FBW
                (0ull << 24) |  // PSM CT32
                (0ull << 32);   // FBMSK

            const uint64_t zbuf1 = (1ull << 32);

            gs.writeRegister(GS_REG_FRAME_1, frame1);
            gs.writeRegister(GS_REG_ZBUF_1, zbuf1);
            gs.writeRegister(GS_REG_TEST_1, 0x30000ull);

            // XYOFFSET=1,1 pixels (16.4 fixed point).
            const uint64_t xyoffset = (16ull) | (16ull << 32);
            gs.writeRegister(GS_REG_XYOFFSET_1, xyoffset);

            // Scissor initially includes pixel (1,1).
            const uint64_t scissorInside = (0ull) | (3ull << 16) | (0ull << 32) | (3ull << 48);
            gs.writeRegister(GS_REG_SCISSOR_1, scissorInside);

            gs.writeRegister(GS_REG_PRIM, static_cast<uint64_t>(GS_PRIM_SPRITE));
            gs.writeRegister(GS_REG_RGBAQ, 0xFF3214C8ull); // RGBA=(200,20,50,255)

            // With XYOFFSET=(1,1), vertex at (2,2) draws to pixel (1,1).
            const uint64_t xyz = (32ull) | (32ull << 16) | (0ull << 32);
            gs.writeRegister(GS_REG_XYZ2, xyz);
            gs.writeRegister(GS_REG_XYZ2, xyz);

            const uint32_t insideOff = frameOffsetBytes(1u, 1u, 1u);
            t.Equals(vram[insideOff + 0u], static_cast<uint8_t>(200u), "inside draw should write R");
            t.Equals(vram[insideOff + 1u], static_cast<uint8_t>(20u), "inside draw should write G");
            t.Equals(vram[insideOff + 2u], static_cast<uint8_t>(50u), "inside draw should write B");
            t.Equals(vram[insideOff + 3u], static_cast<uint8_t>(255u), "inside draw should write A");

            std::memset(vram.data(), 0, 1024u);

            // Move scissor so target pixel is fully outside.
            const uint64_t scissorOutside = (3ull) | (4ull << 16) | (3ull << 32) | (4ull << 48);
            gs.writeRegister(GS_REG_SCISSOR_1, scissorOutside);
            gs.writeRegister(GS_REG_XYZ2, xyz);
            gs.writeRegister(GS_REG_XYZ2, xyz);

            bool anyWrite = false;
            for (size_t i = 0; i < 1024u; ++i)
            {
                if (vram[i] != 0u)
                {
                    anyWrite = true;
                    break;
                }
            }
            t.IsFalse(anyWrite, "fully-outside sprite should not render any pixel");
        });

        tc.Run("GS alpha blend uses ALPHA register FIX factor", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            const uint64_t frame1 =
                (0ull << 0) |   // FBP
                (1ull << 16) |  // FBW
                (0ull << 24) |  // PSM CT32
                (0ull << 32);   // FBMSK
            const uint64_t zbuf1 = (1ull << 32);
            gs.writeRegister(GS_REG_FRAME_1, frame1);
            gs.writeRegister(GS_REG_ZBUF_1, zbuf1);
            gs.writeRegister(GS_REG_SCISSOR_1, (0ull) | (4ull << 16) | (0ull << 32) | (4ull << 48));
            gs.writeRegister(GS_REG_XYOFFSET_1, 0ull);
            gs.writeRegister(GS_REG_TEST_1, 0x30000ull);

            const uint32_t pxOff = frameOffsetBytes(1u, 1u, 1u);
            vram[pxOff + 0u] = 40u;
            vram[pxOff + 1u] = 40u;
            vram[pxOff + 2u] = 40u;
            vram[pxOff + 3u] = 255u;

            // ABE on sprite prim.
            gs.writeRegister(GS_REG_PRIM, static_cast<uint64_t>(GS_PRIM_SPRITE) | (1ull << 6));

            // ALPHA: (A-B)*FIX/128 + D
            // A=Cs(0), B=Cd(1), C=FIX(2), D=Cd(1), FIX=64.
            const uint64_t alpha = (0ull << 0) | (1ull << 2) | (2ull << 4) | (1ull << 6) | (64ull << 32);
            gs.writeRegister(GS_REG_ALPHA_1, alpha);
            gs.writeRegister(GS_REG_RGBAQ, 0xFFC8C8C8ull); // src RGB = 200

            const uint64_t xyz = (16ull) | (16ull << 16) | (0ull << 32); // pixel (1,1)
            gs.writeRegister(GS_REG_XYZ2, xyz);
            gs.writeRegister(GS_REG_XYZ2, xyz);

            // ((200 - 40) * 64 >> 7) + 40 = 120
            t.Equals(vram[pxOff + 0u], static_cast<uint8_t>(120u), "alpha blend should update R with FIX factor");
            t.Equals(vram[pxOff + 1u], static_cast<uint8_t>(120u), "alpha blend should update G with FIX factor");
            t.Equals(vram[pxOff + 2u], static_cast<uint8_t>(120u), "alpha blend should update B with FIX factor");
        });

        tc.Run("sceVu0ApplyMatrix uses libvux matrix math with the imported EE ABI", [](TestCase &t)
        {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            R5900Context ctx{};

            constexpr uint32_t kOutAddr = 0x00100000u;
            constexpr uint32_t kMatrixAddr = 0x00100040u;
            constexpr uint32_t kSrcAddr = 0x00100080u;

            const float matrix[16] = {
                1.0f, 2.0f, 3.0f, 4.0f,
                5.0f, 6.0f, 7.0f, 8.0f,
                9.0f, 10.0f, 11.0f, 12.0f,
                13.0f, 14.0f, 15.0f, 16.0f,
            };
            const float src[4] = {1.0f, 2.0f, 3.0f, 1.0f};
            std::memcpy(rdram.data() + kMatrixAddr, matrix, sizeof(matrix));
            std::memcpy(rdram.data() + kSrcAddr, src, sizeof(src));

            setRegU32(ctx, 4, kOutAddr);
            setRegU32(ctx, 5, kMatrixAddr);
            setRegU32(ctx, 6, kSrcAddr);

            ps2_stubs::sceVu0ApplyMatrix(rdram.data(), &ctx, nullptr);

            float out[4]{};
            std::memcpy(out, rdram.data() + kOutAddr, sizeof(out));
            t.Equals(out[0], 51.0f, "sceVu0ApplyMatrix should compute X with libvux layout");
            t.Equals(out[1], 58.0f, "sceVu0ApplyMatrix should compute Y with libvux layout");
            t.Equals(out[2], 65.0f, "sceVu0ApplyMatrix should compute Z with libvux layout");
            t.Equals(out[3], 72.0f, "sceVu0ApplyMatrix should compute W with libvux layout");
            t.Equals(getRegS32(ctx, 2), 0, "sceVu0ApplyMatrix should report success");
        });

        tc.Run("sceVu0TransposeMatrix transposes a 4x4 matrix with dst/src ABI", [](TestCase &t)
        {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            R5900Context ctx{};

            constexpr uint32_t kDstAddr = 0x00100100u;
            constexpr uint32_t kSrcAddr = 0x00100140u;
            const float src[16] = {
                1.0f, 2.0f, 3.0f, 4.0f,
                5.0f, 6.0f, 7.0f, 8.0f,
                9.0f, 10.0f, 11.0f, 12.0f,
                13.0f, 14.0f, 15.0f, 16.0f,
            };
            std::memcpy(rdram.data() + kSrcAddr, src, sizeof(src));

            setRegU32(ctx, 4, kDstAddr);
            setRegU32(ctx, 5, kSrcAddr);

            ps2_stubs::sceVu0TransposeMatrix(rdram.data(), &ctx, nullptr);

            float out[16]{};
            std::memcpy(out, rdram.data() + kDstAddr, sizeof(out));
            t.Equals(out[0], 1.0f, "transpose should preserve [0][0]");
            t.Equals(out[1], 5.0f, "transpose should swap row 0 col 1");
            t.Equals(out[2], 9.0f, "transpose should swap row 0 col 2");
            t.Equals(out[3], 13.0f, "transpose should swap row 0 col 3");
            t.Equals(out[4], 2.0f, "transpose should swap row 1 col 0");
            t.Equals(out[5], 6.0f, "transpose should preserve [1][1]");
            t.Equals(out[10], 11.0f, "transpose should preserve [2][2]");
            t.Equals(out[12], 4.0f, "transpose should swap row 3 col 0");
            t.Equals(out[15], 16.0f, "transpose should preserve [3][3]");
            t.Equals(getRegS32(ctx, 2), 0, "sceVu0TransposeMatrix should report success");
        });

        tc.Run("sceVif1PkReset preserves the packet base pointer and clears open tag state", [](TestCase &t)
        {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            R5900Context ctx{};

            constexpr uint32_t kStateAddr = 0x00100200u;
            constexpr uint32_t kBaseAddr = 0x00101000u;

            setRegU32(ctx, 4, kStateAddr);
            setRegU32(ctx, 5, kBaseAddr);
            ps2_stubs::sceVif1PkInit(rdram.data(), &ctx, nullptr);

            const uint32_t dirtyCurrent = kBaseAddr + 0x40u;
            const uint32_t dirtyPending = 0x12345678u;
            const uint32_t dirtyDirectOpen = 0x00ABCDEFu;
            const uint32_t dirtyGifOpen = 0x00112233u;
            std::memcpy(rdram.data() + kStateAddr + 0u, &dirtyCurrent, sizeof(dirtyCurrent));
            std::memcpy(rdram.data() + kStateAddr + 8u, &dirtyPending, sizeof(dirtyPending));
            std::memcpy(rdram.data() + kStateAddr + 12u, &dirtyDirectOpen, sizeof(dirtyDirectOpen));
            std::memcpy(rdram.data() + kStateAddr + 20u, &dirtyGifOpen, sizeof(dirtyGifOpen));

            std::memset(&ctx, 0, sizeof(ctx));
            setRegU32(ctx, 4, kStateAddr);
            ps2_stubs::sceVif1PkReset(rdram.data(), &ctx, nullptr);

            uint32_t current = 0u;
            uint32_t base = 0u;
            uint32_t pending = 0u;
            uint32_t directOpen = 0u;
            uint32_t gifOpen = 0u;
            std::memcpy(&current, rdram.data() + kStateAddr + 0u, sizeof(current));
            std::memcpy(&base, rdram.data() + kStateAddr + 4u, sizeof(base));
            std::memcpy(&pending, rdram.data() + kStateAddr + 8u, sizeof(pending));
            std::memcpy(&directOpen, rdram.data() + kStateAddr + 12u, sizeof(directOpen));
            std::memcpy(&gifOpen, rdram.data() + kStateAddr + 20u, sizeof(gifOpen));

            t.Equals(current, kBaseAddr, "sceVif1PkReset should restore current pointer to the packet base");
            t.Equals(base, kBaseAddr, "sceVif1PkReset should preserve the packet base pointer");
            t.Equals(pending, 0u, "sceVif1PkReset should clear pending count tracking");
            t.Equals(directOpen, 0u, "sceVif1PkReset should clear direct-code open state");
            t.Equals(gifOpen, 0u, "sceVif1PkReset should clear GIF-tag open state");
            t.Equals(::getRegU32(&ctx, 2), kBaseAddr, "sceVif1PkReset should return the packet base pointer");
        });

        tc.Run("sceVif1PkCloseDirectCode encodes DIRECT length in qwords", [](TestCase &t)
        {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            R5900Context ctx{};

            constexpr uint32_t kStateAddr = 0x00100400u;
            constexpr uint32_t kBaseAddr = 0x00102000u;

            setRegU32(ctx, 4, kStateAddr);
            setRegU32(ctx, 5, kBaseAddr);
            ps2_stubs::sceVif1PkInit(rdram.data(), &ctx, nullptr);

            std::memset(&ctx, 0, sizeof(ctx));
            setRegU32(ctx, 4, kStateAddr);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::sceVif1PkCnt(rdram.data(), &ctx, nullptr);

            std::memset(&ctx, 0, sizeof(ctx));
            setRegU32(ctx, 4, kStateAddr);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::sceVif1PkOpenDirectCode(rdram.data(), &ctx, nullptr);

            std::memset(&ctx, 0, sizeof(ctx));
            setRegU32(ctx, 4, kStateAddr);
            setRegU32(ctx, 5, 4u); // reserve one qword worth of GIF payload
            ps2_stubs::sceVif1PkReserve(rdram.data(), &ctx, nullptr);

            std::memset(&ctx, 0, sizeof(ctx));
            setRegU32(ctx, 4, kStateAddr);
            ps2_stubs::sceVif1PkCloseDirectCode(rdram.data(), &ctx, nullptr);

            uint32_t directCmd = 0u;
            std::memcpy(&directCmd, rdram.data() + kBaseAddr + 12u, sizeof(directCmd));
            t.Equals(directCmd, 0x50000001u, "sceVif1PkCloseDirectCode should store a 1-QW DIRECT length");
        });
    });
}
