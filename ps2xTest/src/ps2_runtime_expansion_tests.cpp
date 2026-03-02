#include "MiniTest.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/r5900_decoder.h"
#include "ps2recomp/types.h"
#include "ps2_runtime.h"
#include "ps2_memory.h"
#include "ps2_syscalls.h"
#include "ps2_gs_gpu.h"
#include "ps2_runtime_macros.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <string>
#include <thread>
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

    bool hasSignedRdWrite(const std::string &generated, uint8_t rd)
    {
        if (rd == 0u)
        {
            return false;
        }

        const std::string needle = "SET_GPR_S32(ctx, " + std::to_string(rd) + ",";
        return generated.find(needle) != std::string::npos;
    }

    template <typename Predicate>
    bool waitUntil(Predicate pred, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (pred())
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return pred();
    }

    uint32_t frameOffsetBytes(uint32_t x, uint32_t y, uint32_t fbw)
    {
        const uint32_t stride = fbw * 64u * 4u; // CT32
        return y * stride + x * 4u;
    }

    void testRuntimeWorkerLoop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (!ctx || !runtime)
        {
            return;
        }

        // Keep touching guest memory so teardown races are easier to catch.
        (void)Ps2FastRead64(rdram, static_cast<uint32_t>(0x01FFFFF8u + (ctx->insn_count & 0x7u)));
        ++ctx->insn_count;

        if (runtime->isStopRequested())
        {
            ctx->pc = 0u;
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
            mem.vif1_regs.itop = 0x21u;
            mem.vif1_regs.stat &= ~(1u << 7); // DBF = 0

            uint32_t callbackPc = 0xFFFFFFFFu;
            uint32_t callbackItop = 0xFFFFFFFFu;
            uint32_t callbackCount = 0u;
            mem.setVu1MscalCallback([&](uint32_t startPC, uint32_t itop)
            {
                callbackPc = startPC;
                callbackItop = itop;
                callbackCount++;
            });

            const uint32_t mscal = makeVifCmd(0x14u, 0u, 3u); // start PC = 3 * 8
            mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&mscal), sizeof(mscal));

            t.Equals(callbackCount, 1u, "MSCAL should invoke VU1 callback exactly once");
            t.Equals(callbackPc, 24u, "MSCAL should pass startPC=imm*8");
            t.Equals(callbackItop, 0x21u, "MSCAL callback should receive current ITOP");
            t.Equals(mem.vif1_regs.itops, 0x21u, "MSCAL should latch ITOPS from ITOP");
            t.IsTrue((mem.vif1_regs.stat & (1u << 7)) != 0u, "MSCAL should toggle DBF on");
            t.Equals(mem.vif1_regs.tops, 6u, "DBF=1 should make TOPS=BASE+OFST");

            const uint32_t mscnt = makeVifCmd(0x17u, 0u, 0u);
            mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&mscnt), sizeof(mscnt));

            t.Equals(callbackCount, 1u, "MSCNT should not invoke MSCAL callback");
            t.IsTrue((mem.vif1_regs.stat & (1u << 7)) == 0u, "MSCNT should toggle DBF back off");
            t.Equals(mem.vif1_regs.tops, 4u, "DBF=0 should make TOPS=BASE");
            t.Equals(mem.vif1_regs.itops, 0x21u, "MSCNT should refresh ITOPS from ITOP");
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
            gs.writeRegister(GS_REG_FRAME_1, frame1);

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
            gs.writeRegister(GS_REG_FRAME_1, frame1);
            gs.writeRegister(GS_REG_SCISSOR_1, (0ull) | (4ull << 16) | (0ull << 32) | (4ull << 48));
            gs.writeRegister(GS_REG_XYOFFSET_1, 0ull);

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

        tc.Run("notifyRuntimeStop joins guest worker threads before teardown", [](TestCase &t)
        {
            notifyRuntimeStop();
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);

            constexpr uint32_t kEntry = 0x250000u;
            constexpr uint32_t kThreadParamAddr = 0x2600u;
            const uint32_t threadParam[7] = {
                0u,          // attr
                kEntry,      // entry
                0x00100000u, // stack
                0x00000400u, // stack size
                0x00110000u, // gp
                8u,          // priority
                0u           // option
            };

            runtime.registerFunction(kEntry, &testRuntimeWorkerLoop);
            std::memcpy(rdram.data() + kThreadParamAddr, threadParam, sizeof(threadParam));

            R5900Context createCtx{};
            setRegU32(createCtx, 4, kThreadParamAddr);
            CreateThread(rdram.data(), &createCtx, &runtime);
            const int32_t tid = getRegS32(createCtx, 2);
            t.IsTrue(tid > 0, "CreateThread should succeed for teardown-join test");

            R5900Context startCtx{};
            setRegU32(startCtx, 4, static_cast<uint32_t>(tid));
            setRegU32(startCtx, 5, 0u);
            StartThread(rdram.data(), &startCtx, &runtime);
            t.Equals(getRegS32(startCtx, 2), KE_OK, "StartThread should launch worker");

            const bool started = waitUntil([&]()
            {
                return g_activeThreads.load(std::memory_order_relaxed) > 0;
            }, std::chrono::milliseconds(500));
            t.IsTrue(started, "worker thread should become active");

            runtime.requestStop();
            const bool drained = waitUntil([&]()
            {
                return g_activeThreads.load(std::memory_order_relaxed) == 0;
            }, std::chrono::milliseconds(2000));
            t.IsTrue(drained, "requestStop should drain all guest worker threads");

            notifyRuntimeStop();
        });

        tc.Run("Semaphore poll/signal remains stable under host-thread contention", [](TestCase &t)
        {
            notifyRuntimeStop();
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);

            constexpr uint32_t kParamAddr = 0x2000u;
            const uint32_t semaParam[6] = {
                0u, // count
                1u, // max_count
                1u, // init_count
                0u, // wait_threads
                0u, // attr
                0u  // option
            };
            std::memcpy(rdram.data() + kParamAddr, semaParam, sizeof(semaParam));

            R5900Context createCtx{};
            setRegU32(createCtx, 4, kParamAddr);
            CreateSema(rdram.data(), &createCtx, &runtime);
            const int32_t sid = getRegS32(createCtx, 2);
            t.IsTrue(sid > 0, "CreateSema should return a valid sid");

            std::atomic<int32_t> pollOkCount{0};
            std::atomic<int32_t> signalOkCount{0};
            std::atomic<bool> pollerThrew{false};
            std::atomic<bool> signalerThrew{false};

            std::thread poller([&]()
            {
                try
                {
                    for (int i = 0; i < 64; ++i)
                    {
                        R5900Context pollCtx{};
                        setRegU32(pollCtx, 4, static_cast<uint32_t>(sid));
                        PollSema(rdram.data(), &pollCtx, &runtime);
                        if (getRegS32(pollCtx, 2) == KE_OK)
                        {
                            pollOkCount.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }
                catch (...)
                {
                    pollerThrew.store(true, std::memory_order_release);
                }
            });

            std::thread signaler([&]()
            {
                try
                {
                    for (int i = 0; i < 64; ++i)
                    {
                        R5900Context signalCtx{};
                        setRegU32(signalCtx, 4, static_cast<uint32_t>(sid));
                        SignalSema(rdram.data(), &signalCtx, &runtime);
                        if (getRegS32(signalCtx, 2) == KE_OK)
                        {
                            signalOkCount.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }
                catch (...)
                {
                    signalerThrew.store(true, std::memory_order_release);
                }
            });

            if (poller.joinable())
            {
                poller.join();
            }
            if (signaler.joinable())
            {
                signaler.join();
            }

            t.IsFalse(pollerThrew.load(std::memory_order_acquire),
                      "PollSema worker thread should not throw");
            t.IsFalse(signalerThrew.load(std::memory_order_acquire),
                      "SignalSema worker thread should not throw");
            t.IsTrue(pollOkCount.load(std::memory_order_relaxed) > 0,
                     "contended PollSema should observe at least one successful acquire");
            t.IsTrue(signalOkCount.load(std::memory_order_relaxed) > 0,
                     "contended SignalSema should observe successful releases");

            constexpr uint32_t kStatusAddr = 0x2100u;
            R5900Context referCtx{};
            setRegU32(referCtx, 4, static_cast<uint32_t>(sid));
            setRegU32(referCtx, 5, kStatusAddr);
            ReferSemaStatus(rdram.data(), &referCtx, &runtime);
            t.Equals(getRegS32(referCtx, 2), KE_OK, "ReferSemaStatus should succeed after contention");

            int32_t finalCount = 0;
            std::memcpy(&finalCount, rdram.data() + kStatusAddr + 0u, sizeof(finalCount));
            t.IsTrue(finalCount >= 0 && finalCount <= 1, "semaphore count should remain within [0, max_count]");

            runtime.requestStop();
            notifyRuntimeStop();
        });

        tc.Run("WaitEventFlag AND-mode is stable under concurrent setters", [](TestCase &t)
        {
            notifyRuntimeStop();
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);

            constexpr uint32_t kEventParamAddr = 0x2400u;
            constexpr uint32_t kResBitsAddr = 0x2410u;
            const uint32_t eventParam[3] = {0u, 0u, 0u};
            std::memcpy(rdram.data() + kEventParamAddr, eventParam, sizeof(eventParam));

            R5900Context createCtx{};
            setRegU32(createCtx, 4, kEventParamAddr);
            CreateEventFlag(rdram.data(), &createCtx, &runtime);
            const int32_t eid = getRegS32(createCtx, 2);
            t.IsTrue(eid > 0, "CreateEventFlag should return a valid id");

            std::atomic<bool> waiterDone{false};
            std::atomic<int32_t> waiterRet{-9999};
            std::atomic<uint32_t> waiterBits{0u};
            std::atomic<bool> waiterThrew{false};
            std::atomic<bool> setterAThrew{false};
            std::atomic<bool> setterBThrew{false};

            std::thread waiter([&]()
            {
                try
                {
                    R5900Context waitCtx{};
                    setRegU32(waitCtx, 4, static_cast<uint32_t>(eid));
                    setRegU32(waitCtx, 5, 0x3u); // wait for bit0 and bit1 (AND mode)
                    setRegU32(waitCtx, 6, 0u);   // AND, no clear
                    setRegU32(waitCtx, 7, kResBitsAddr);
                    WaitEventFlag(rdram.data(), &waitCtx, &runtime);
                    waiterRet.store(getRegS32(waitCtx, 2), std::memory_order_relaxed);
                    uint32_t bits = 0u;
                    std::memcpy(&bits, rdram.data() + kResBitsAddr, sizeof(bits));
                    waiterBits.store(bits, std::memory_order_relaxed);
                }
                catch (...)
                {
                    waiterThrew.store(true, std::memory_order_release);
                }
                waiterDone.store(true, std::memory_order_release);
            });

            std::thread setterA([&]()
            {
                try
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    R5900Context setCtx{};
                    setRegU32(setCtx, 4, static_cast<uint32_t>(eid));
                    setRegU32(setCtx, 5, 0x1u);
                    SetEventFlag(rdram.data(), &setCtx, &runtime);
                }
                catch (...)
                {
                    setterAThrew.store(true, std::memory_order_release);
                }
            });

            std::thread setterB([&]()
            {
                try
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(15));
                    R5900Context setCtx{};
                    setRegU32(setCtx, 4, static_cast<uint32_t>(eid));
                    setRegU32(setCtx, 5, 0x2u);
                    SetEventFlag(rdram.data(), &setCtx, &runtime);
                }
                catch (...)
                {
                    setterBThrew.store(true, std::memory_order_release);
                }
            });

            const bool woke = waitUntil([&]()
            {
                return waiterDone.load(std::memory_order_acquire);
            }, std::chrono::milliseconds(500));

            if (setterA.joinable())
            {
                setterA.join();
            }
            if (setterB.joinable())
            {
                setterB.join();
            }
            if (waiter.joinable())
            {
                waiter.join();
            }

            t.IsFalse(waiterThrew.load(std::memory_order_acquire),
                      "WaitEventFlag waiter thread should not throw");
            t.IsFalse(setterAThrew.load(std::memory_order_acquire),
                      "SetEventFlag setterA thread should not throw");
            t.IsFalse(setterBThrew.load(std::memory_order_acquire),
                      "SetEventFlag setterB thread should not throw");
            t.IsTrue(woke, "WaitEventFlag AND waiter should wake after both bits are published");
            t.Equals(waiterRet.load(std::memory_order_relaxed), KE_OK, "WaitEventFlag should return KE_OK");
            t.IsTrue((waiterBits.load(std::memory_order_relaxed) & 0x3u) == 0x3u,
                     "WaitEventFlag result bits should include both concurrently-set bits");

            R5900Context deleteCtx{};
            setRegU32(deleteCtx, 4, static_cast<uint32_t>(eid));
            DeleteEventFlag(rdram.data(), &deleteCtx, &runtime);
            runtime.requestStop();
            notifyRuntimeStop();
        });
    });
}
