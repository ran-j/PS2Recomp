#include "MiniTest.h"
#include "ps2_stubs.h"
#include "ps2_syscalls.h"
#include "Stubs/Pad.h"

#include <vector>
#include <cstdint>
#include <string>
#include <cstdio>


namespace
{
    constexpr uint32_t kPadDataAddr = 0x1000;

    constexpr uint16_t kPadBtnSelect = 1u << 0;
    constexpr uint16_t kPadBtnL3 = 1u << 1;
    constexpr uint16_t kPadBtnR3 = 1u << 2;
    constexpr uint16_t kPadBtnStart = 1u << 3;
    constexpr uint16_t kPadBtnUp = 1u << 4;
    constexpr uint16_t kPadBtnRight = 1u << 5;
    constexpr uint16_t kPadBtnDown = 1u << 6;
    constexpr uint16_t kPadBtnLeft = 1u << 7;
    constexpr uint16_t kPadBtnL2 = 1u << 8;
    constexpr uint16_t kPadBtnR2 = 1u << 9;
    constexpr uint16_t kPadBtnL1 = 1u << 10;
    constexpr uint16_t kPadBtnR1 = 1u << 11;
    constexpr uint16_t kPadBtnTriangle = 1u << 12;
    constexpr uint16_t kPadBtnCircle = 1u << 13;
    constexpr uint16_t kPadBtnCross = 1u << 14;
    constexpr uint16_t kPadBtnSquare = 1u << 15;

    void setRegU32(R5900Context &ctx, int reg, uint32_t value)
    {
        ctx.r[reg] = _mm_set_epi64x(0, static_cast<int64_t>(value));
    }

    void openPadPort(R5900Context &ctx, std::vector<uint8_t> &rdram, uint32_t port = 0, uint32_t slot = 0)
    {
        setRegU32(ctx, 4, port);
        setRegU32(ctx, 5, slot);
        setRegU32(ctx, 6, kPadDataAddr + 0x200u);
        ps2_stubs::scePadPortOpen(rdram.data(), &ctx, nullptr);
    }

    void closePadPort(R5900Context &ctx, std::vector<uint8_t> &rdram, uint32_t port = 0, uint32_t slot = 0)
    {
        setRegU32(ctx, 4, port);
        setRegU32(ctx, 5, slot);
        ps2_stubs::scePadPortClose(rdram.data(), &ctx, nullptr);
    }

    void runPadRead(R5900Context &ctx, std::vector<uint8_t> &rdram)
    {
        setRegU32(ctx, 4, 0u);
        setRegU32(ctx, 5, 0u);
        setRegU32(ctx, 6, kPadDataAddr); // a2
        ps2_stubs::scePadRead(rdram.data(), &ctx, nullptr);
    }

    uint16_t readButtons(const std::vector<uint8_t> &rdram)
    {
        const uint8_t *data = rdram.data() + kPadDataAddr;
        return static_cast<uint16_t>(data[2] | (data[3] << 8));
    }

    using PadHandler = void (*)(uint8_t *, R5900Context *, PS2Runtime *);

    // Stuntman's per-frame pad poll sub_00126110, line for line from IDA:
    // pad object state byte pad+61, mode id pad+62, buffer mode nibble
    // pad+63, combined-mode flag pad+48, pressure flag pad+52, and the
    // 8-byte manager record whose type (record+4) the front end tests.
    // Every libpad call uses the guest's own arguments (port 0, slot 0,
    // SetMainMode lock 3 from sub_00125F58, buffer at pad+64).
    struct StuntmanPadPoll
    {
        std::vector<uint8_t> &rdram;
        R5900Context &ctx;
        uint8_t state = 0;
        uint8_t id = 0;
        uint8_t mode = 0;
        bool comb = false;
        bool pressure = false;
        uint32_t recordType = 0;
        int setMainModeCalls = 0;
        int setActAlignCalls = 0;
        int enterPressModeCalls = 0;

        uint32_t call(PadHandler fn, uint32_t a2 = 0, uint32_t a3 = 0)
        {
            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, a2);
            setRegU32(ctx, 7, a3);
            fn(rdram.data(), &ctx, nullptr);
            return getRegU32(&ctx, 2);
        }

        void poll()
        {
            const uint32_t padState = call(ps2_stubs::scePadGetState);
            if (padState == 0u)
            {
                mode = 0;
                comb = false;
                pressure = false;
                state = 0;
                recordType = 0;
                return;
            }
            uint8_t *buf = rdram.data() + kPadDataAddr;
            if (call(ps2_stubs::scePadRead, kPadDataAddr) != 0u || call(ps2_stubs::scePadRead, kPadDataAddr) != 0u)
            {
                mode = static_cast<uint8_t>(buf[1] >> 4);
            }
            else
            {
                mode = 0;
                buf[0] = 0xFF;
            }
            const bool stable = (padState == 6u || padState == 2u);
            switch (state)
            {
            case 0:
                if (!stable)
                {
                    buf[0] = 0xFF;
                    break;
                }
                id = static_cast<uint8_t>(call(ps2_stubs::scePadInfoMode, 1u, 0u));
                if (id != 0)
                {
                    const int32_t exId = static_cast<int32_t>(call(ps2_stubs::scePadInfoMode, 2u, 0u));
                    if (exId > 0)
                        id = static_cast<uint8_t>(exId);
                    if (id == 4)
                        state = 40;
                    else if (id == 7)
                        state = 70;
                    else
                        buf[0] = 0xFF;
                }
                break;
            case 40:
                if (call(ps2_stubs::scePadInfoMode, 2u, 0u) == 0u)
                {
                    state = 99;
                    break;
                }
                state = 41;
                [[fallthrough]];
            case 41:
                ++setMainModeCalls;
                state = (call(ps2_stubs::scePadSetMainMode, 1u, 3u) == 1u) ? 42 : 98;
                break;
            case 42: // FAILED (1) re-issues from 41, BUSY waits, COMPLETE re-reads the mode
                if (call(ps2_stubs::scePadGetReqState) == 1u)
                    --state;
                if (call(ps2_stubs::scePadGetReqState) == 0u)
                    state = 0;
                break;
            case 70:
                if (call(ps2_stubs::scePadInfoAct, static_cast<uint32_t>(-1), 0u) == 0u)
                {
                    state = 99;
                    break;
                }
                ++setActAlignCalls;
                if (call(ps2_stubs::scePadSetActAlign, kPadDataAddr + 0x30u) == 1u)
                    state = 71;
                break;
            case 71:
                if (call(ps2_stubs::scePadGetReqState) == 1u)
                {
                    --state;
                    break;
                }
                if (call(ps2_stubs::scePadGetReqState) == 0u)
                    ++state;
                break;
            case 72:
                pressure = call(ps2_stubs::scePadInfoPressMode) != 0u;
                state = pressure ? 76 : 98;
                break;
            case 76:
                ++enterPressModeCalls;
                state = (call(ps2_stubs::scePadEnterPressMode) != 0u) ? 77 : 72;
                break;
            case 77:
                if (call(ps2_stubs::scePadGetReqState) == 1u)
                    --state;
                if (call(ps2_stubs::scePadGetReqState) == 0u)
                    ++state;
                break;
            case 78:
                state = 98;
                break;
            case 98:
                if (static_cast<int32_t>(call(ps2_stubs::scePadInfoComb, static_cast<uint32_t>(-1), 0u)) > 0 &&
                    static_cast<int32_t>(call(ps2_stubs::scePadInfoComb, 0u, static_cast<uint32_t>(-1))) >= 2)
                    comb = true;
                state = 99;
                [[fallthrough]];
            default:
                if (stable && mode != id)
                {
                    state = 0;
                    buf[0] = 0xFF;
                    comb = false;
                    pressure = false;
                }
                break;
            }
            if (mode == 4 || mode == 7)
                recordType = pressure ? 4u : (comb ? 3u : 2u);
            else if (mode != 0)
                recordType = 1u;
        }
    };
}

void register_pad_input_tests()
{
    MiniTest::Case("PadInput", [](TestCase &tc)
                   {
        tc.Run("scePadRead uses override state", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            ps2_stubs::scePadInit(rdram.data(), &ctx, nullptr);
            openPadPort(ctx, rdram);

            const uint16_t buttons = static_cast<uint16_t>(0xFFFFu & ~kPadBtnCross & ~kPadBtnStart);
            ps2_stubs::setPadOverrideState(buttons, 0x00, 0xFF, 0x10, 0xEE);

            runPadRead(ctx, rdram);

            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadRead should return 1");
            t.Equals(readButtons(rdram), buttons, "button bitmask should match override state");
            const uint8_t *data = rdram.data() + kPadDataAddr;
            t.Equals(data[4], static_cast<uint8_t>(0x10), "rx should match override");
            t.Equals(data[5], static_cast<uint8_t>(0xEE), "ry should match override");
            t.Equals(data[6], static_cast<uint8_t>(0x00), "lx should match override");
            t.Equals(data[7], static_cast<uint8_t>(0xFF), "ly should match override");

            ps2_stubs::clearPadOverrideState();
            closePadPort(ctx, rdram);
        });

        tc.Run("scePadRead button bits are active-low", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            ps2_stubs::scePadInit(rdram.data(), &ctx, nullptr);
            openPadPort(ctx, rdram);

            struct ButtonCase
            {
                uint16_t mask;
                const char *name;
            };

            const ButtonCase cases[] = {
                {kPadBtnSelect, "select"},
                {kPadBtnL3, "l3"},
                {kPadBtnR3, "r3"},
                {kPadBtnStart, "start"},
                {kPadBtnUp, "up"},
                {kPadBtnRight, "right"},
                {kPadBtnDown, "down"},
                {kPadBtnLeft, "left"},
                {kPadBtnL2, "l2"},
                {kPadBtnR2, "r2"},
                {kPadBtnL1, "l1"},
                {kPadBtnR1, "r1"},
                {kPadBtnTriangle, "triangle"},
                {kPadBtnCircle, "circle"},
                {kPadBtnCross, "cross"},
                {kPadBtnSquare, "square"}};

            for (const auto &entry : cases)
            {
                const uint16_t buttons = static_cast<uint16_t>(0xFFFFu & ~entry.mask);
                ps2_stubs::setPadOverrideState(buttons, 0x80, 0x80, 0x80, 0x80);
                runPadRead(ctx, rdram);

                t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadRead should succeed for opened ports");
                const uint16_t mask = readButtons(rdram);
                t.IsTrue((mask & entry.mask) == 0, std::string("button should be active-low: ").append(entry.name));
            }

            ps2_stubs::clearPadOverrideState();
            closePadPort(ctx, rdram);
        });

        tc.Run("scePadGetButtonMask returns all buttons", [](TestCase &t)
               {
            R5900Context ctx;
            ps2_stubs::scePadGetButtonMask(nullptr, &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(0xFFFF), "button mask should be 0xFFFF");
        });

        tc.Run("basic pad init/port/state functions return expected values", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            ps2_stubs::scePadInit(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadInit should succeed");

            ps2_stubs::scePadInit2(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadInit2 should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadGetState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(0), "closed port should report DISCONNECTED");

            openPadPort(ctx, rdram);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadPortOpen should succeed");

            // Frame progression: a freshly opened port is FINDPAD (1)
            // for the first polls, then STABLE (6) - not instant STABLE.
            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadGetState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "first state after open should be FINDPAD");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadGetState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "second state after open should be FINDPAD");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadGetState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(6), "scePadGetState should settle to STABLE");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadGetReqState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(0), "scePadGetReqState should return completed");

            ps2_stubs::scePadGetPortMax(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(2), "scePadGetPortMax should be 2");

            setRegU32(ctx, 4, 0u);
            ps2_stubs::scePadGetSlotMax(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadGetSlotMax should be 1");

            ps2_stubs::scePadGetModVersion(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(0x0200), "scePadGetModVersion should be 0x0200");

            closePadPort(ctx, rdram);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadPortClose should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadGetState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(0), "closed port should return DISCONNECTED after close");
        });

        // Stuntman's own libpad (scePadGetState @0x00243D48) reports
        // EXECCMD (5) exactly while the pad is STABLE and the request byte
        // is BUSY (2); padman completes the request a few frames later.
        tc.Run("pad command state reports EXECCMD while the request is BUSY, then STABLE", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;
            auto call2 = [&](PadHandler fn, uint32_t a2 = 0, uint32_t a3 = 0) -> uint32_t {
                setRegU32(ctx, 4, 0u);
                setRegU32(ctx, 5, 0u);
                setRegU32(ctx, 6, a2);
                setRegU32(ctx, 7, a3);
                fn(rdram.data(), &ctx, nullptr);
                return getRegU32(&ctx, 2);
            };
            // One guest frame per GetState poll while the request is BUSY;
            // returns how many frames reported EXECCMD.
            auto framesUntilComplete = [&](const char *what) -> int {
                int execFrames = 0;
                for (int frame = 0; frame < 16; ++frame)
                {
                    const uint32_t state = call2(ps2_stubs::scePadGetState);
                    const uint32_t req = call2(ps2_stubs::scePadGetReqState);
                    if (req == 0u)
                    {
                        t.Equals(state, 6u, std::string(what) + ": COMPLETE frame should report STABLE (6)");
                        return execFrames;
                    }
                    t.Equals(req, 2u, std::string(what) + ": in-flight request should be BUSY (2), never FAILED (1)");
                    t.Equals(state, 5u, std::string(what) + ": BUSY frame should report EXECCMD (5)");
                    ++execFrames;
                }
                t.Fail(std::string(what) + ": request never completed");
                return execFrames;
            };

            ps2_stubs::scePadInit(rdram.data(), &ctx, nullptr);
            openPadPort(ctx, rdram);

            // Drain the FINDPAD connect progression so the command
            // below is observed in isolation.
            for (int poll = 0; poll < 3; ++poll)
                call2(ps2_stubs::scePadGetState);
            t.Equals(call2(ps2_stubs::scePadGetReqState), 0u, "no request in flight: COMPLETE (0)");

            t.Equals(call2(ps2_stubs::scePadSetMainMode, 1u, 3u), 1u, "scePadSetMainMode should succeed");
            t.Equals(call2(ps2_stubs::scePadGetReqState), 2u, "accepted mode request is BUSY (2) at once");
            const int modeFrames = framesUntilComplete("mode request");
            t.IsTrue(modeFrames >= 1, "mode request should stay BUSY for at least one frame");

            t.Equals(call2(ps2_stubs::scePadEnterPressMode), 1u, "scePadEnterPressMode should succeed");
            t.Equals(call2(ps2_stubs::scePadGetReqState), 2u, "accepted press-mode request is BUSY (2) at once");
            const int pressFrames = framesUntilComplete("press-mode request");
            t.IsTrue(pressFrames >= 1, "press-mode request should stay BUSY for at least one frame");
            t.Equals(call2(ps2_stubs::scePadGetState), 6u, "idle port stays STABLE");

            // A guest-written request byte lasts until padman's next frame.
            t.Equals(call2(ps2_stubs::scePadSetReqState, 1u), 1u, "scePadSetReqState on an open port should succeed");
            t.Equals(call2(ps2_stubs::scePadGetReqState), 1u, "stored request byte reads back (FAILED)");
            call2(ps2_stubs::scePadGetState);
            t.Equals(call2(ps2_stubs::scePadGetReqState), 0u, "next frame reports padman's COMPLETE again");

            closePadPort(ctx, rdram);
            t.Equals(call2(ps2_stubs::scePadSetReqState, 2u), 0u, "scePadSetReqState on a closed port should fail");
        });

        tc.Run("pad info and mode helpers return consistent values", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            ps2_stubs::scePadInit(rdram.data(), &ctx, nullptr);
            openPadPort(ctx, rdram);

            setRegU32(ctx, 6, static_cast<uint32_t>(-1));
            ps2_stubs::scePadInfoAct(rdram.data(), &ctx, nullptr);
            t.IsTrue(static_cast<uint32_t>(getRegU32(&ctx, 2)) >= 1u, "scePadInfoAct should report at least one actuator descriptor");

            ps2_stubs::scePadInfoComb(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(0), "scePadInfoComb should return 0");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 1);
            setRegU32(ctx, 7, 0);
            ps2_stubs::scePadInfoMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(4), "scePadInfoMode CURID should return digital at open");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 4);
            setRegU32(ctx, 7, static_cast<uint32_t>(-1));
            ps2_stubs::scePadInfoMode(rdram.data(), &ctx, nullptr);
            t.IsTrue(static_cast<uint32_t>(getRegU32(&ctx, 2)) >= 1u, "scePadInfoMode table count should be non-zero");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadInfoPressMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadInfoPressMode should report pressure support");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 0u);
            setRegU32(ctx, 7, 3u);
            ps2_stubs::scePadSetMainMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetMainMode should accept digital mode");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 1u);
            setRegU32(ctx, 7, 0u);
            ps2_stubs::scePadInfoMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(4), "CURID should switch to digital mode");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 1u);
            setRegU32(ctx, 7, 3u);
            ps2_stubs::scePadSetMainMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetMainMode should accept analog mode");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 4);
            setRegU32(ctx, 7, 0u);
            ps2_stubs::scePadInfoMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(7), "mode table entry should return DualShock in analog mode");

            closePadPort(ctx, rdram);
        });

        tc.Run("pads open in digital mode and switch to analog on scePadSetMainMode", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            ps2_stubs::scePadInit(rdram.data(), &ctx, nullptr);
            openPadPort(ctx, rdram);

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadGetState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "freshly opened port should report FINDPAD");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadGetState(rdram.data(), &ctx, nullptr);
            ps2_stubs::scePadGetState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(6), "opened port should settle to STABLE");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 1);
            setRegU32(ctx, 7, 0);
            ps2_stubs::scePadInfoMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(4), "scePadInfoMode CURID should return digital at open");

            runPadRead(ctx, rdram);
            const uint8_t *data = rdram.data() + kPadDataAddr;
            t.Equals(data[1], static_cast<uint8_t>(0x41), "mode byte should be 0x41 (digital) at open");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 1);
            setRegU32(ctx, 7, 3);
            ps2_stubs::scePadSetMainMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetMainMode should succeed switching to analog");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 1);
            setRegU32(ctx, 7, 0);
            ps2_stubs::scePadInfoMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(7), "scePadInfoMode CURID should return analog after SetMainMode");

            // One guest frame passes before the next read; the mode byte
            // already follows scePadSetMainMode (the request itself stays
            // BUSY for a few frames, see the EXECCMD test above).
            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadGetState(rdram.data(), &ctx, nullptr);

            runPadRead(ctx, rdram);
            t.Equals(data[1], static_cast<uint8_t>(0x73), "mode byte should be 0x73 (analog) after SetMainMode");

            closePadPort(ctx, rdram);
        });

        tc.Run("pad setters return success", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            ps2_stubs::scePadInit(rdram.data(), &ctx, nullptr);
            openPadPort(ctx, rdram);

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadSetActAlign(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetActAlign should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadSetActDirect(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetActDirect should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 0xFFFFu);
            ps2_stubs::scePadSetButtonInfo(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetButtonInfo should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 1u);
            setRegU32(ctx, 7, 3u);
            ps2_stubs::scePadSetMainMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetMainMode should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadSetReqState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetReqState should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadSetVrefParam(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetVrefParam should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadSetWarningLevel(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(0), "scePadSetWarningLevel should return 0");

            ps2_stubs::scePadEnd(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadEnd should succeed");

            openPadPort(ctx, rdram);
            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadEnterPressMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadEnterPressMode should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadExitPressMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadExitPressMode should succeed");

            closePadPort(ctx, rdram);
        });

        tc.Run("scePadRead fills pressure bytes and honors button info mask", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            ps2_stubs::scePadInit(rdram.data(), &ctx, nullptr);
            openPadPort(ctx, rdram);

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 0xFFFFu);
            ps2_stubs::scePadSetButtonInfo(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetButtonInfo should accept all buttons");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadEnterPressMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadEnterPressMode should enable pressure data");

            const uint16_t pressedButtons = static_cast<uint16_t>(0xFFFFu &
                                                                   ~kPadBtnLeft &
                                                                   ~kPadBtnUp &
                                                                   ~kPadBtnTriangle &
                                                                   ~kPadBtnCross &
                                                                   ~kPadBtnL1 &
                                                                   ~kPadBtnR1 &
                                                                   ~kPadBtnR2);
            ps2_stubs::setPadOverrideState(pressedButtons, 0x80, 0x80, 0x80, 0x80);
            runPadRead(ctx, rdram);

            const uint8_t *data = rdram.data() + kPadDataAddr;
            t.Equals(data[8], static_cast<uint8_t>(0x00), "right pressure should be clear when not pressed");
            t.Equals(data[9], static_cast<uint8_t>(0xFF), "left pressure should be populated when pressed");
            t.Equals(data[10], static_cast<uint8_t>(0xFF), "up pressure should be populated when pressed");
            t.Equals(data[11], static_cast<uint8_t>(0x00), "down pressure should be clear when not pressed");
            t.Equals(data[12], static_cast<uint8_t>(0xFF), "triangle pressure should be populated when pressed");
            t.Equals(data[13], static_cast<uint8_t>(0x00), "circle pressure should be clear when not pressed");
            t.Equals(data[14], static_cast<uint8_t>(0xFF), "cross pressure should be populated when pressed");
            t.Equals(data[15], static_cast<uint8_t>(0x00), "square pressure should be clear when not pressed");
            // libpad order for bytes 16-19 is L1, R1, L2, R2 (Stuntman's
            // sub_00126560 consumes them in that order).
            t.Equals(data[16], static_cast<uint8_t>(0xFF), "L1 pressure (byte 16) should be populated when pressed");
            t.Equals(data[17], static_cast<uint8_t>(0xFF), "R1 pressure (byte 17) should be populated when pressed");
            t.Equals(data[18], static_cast<uint8_t>(0x00), "L2 pressure (byte 18) should be clear when not pressed");
            t.Equals(data[19], static_cast<uint8_t>(0xFF), "R2 pressure (byte 19) should be populated when pressed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, static_cast<uint32_t>(kPadBtnL1 | kPadBtnR2));
            ps2_stubs::scePadSetButtonInfo(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetButtonInfo should narrow the enabled pressure mask");

            runPadRead(ctx, rdram);

            t.Equals(data[9], static_cast<uint8_t>(0x00), "masked-out direction pressure should clear");
            t.Equals(data[10], static_cast<uint8_t>(0x00), "masked-out direction pressure should clear");
            t.Equals(data[12], static_cast<uint8_t>(0x00), "masked-out face-button pressure should clear");
            t.Equals(data[14], static_cast<uint8_t>(0x00), "masked-out face-button pressure should clear");
            t.Equals(data[17], static_cast<uint8_t>(0x00), "masked-out R1 pressure should clear");
            t.Equals(data[16], static_cast<uint8_t>(0xFF), "enabled L1 pressure should remain populated");
            t.Equals(data[19], static_cast<uint8_t>(0xFF), "enabled R2 pressure should remain populated");

            ps2_stubs::clearPadOverrideState();
            closePadPort(ctx, rdram);
        });

        tc.Run("pad string helpers map state codes", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            setRegU32(ctx, 4, 1);
            setRegU32(ctx, 5, kPadDataAddr);
            ps2_stubs::scePadStateIntToStr(rdram.data(), &ctx, nullptr);
            t.IsTrue(std::string(reinterpret_cast<const char *>(rdram.data() + kPadDataAddr)).find("FINDPAD") != std::string::npos,
                     "state 1 should map to FINDPAD");

            setRegU32(ctx, 4, 0);
            setRegU32(ctx, 5, kPadDataAddr + 64);
            ps2_stubs::scePadStateIntToStr(rdram.data(), &ctx, nullptr);
            t.IsTrue(std::string(reinterpret_cast<const char *>(rdram.data() + kPadDataAddr + 64)).find("DISCONNECTED") != std::string::npos,
                     "state 0 should map to DISCONNECTED");

            setRegU32(ctx, 4, 2);
            setRegU32(ctx, 5, kPadDataAddr + 128);
            ps2_stubs::scePadReqIntToStr(rdram.data(), &ctx, nullptr);
            t.IsTrue(std::string(reinterpret_cast<const char *>(rdram.data() + kPadDataAddr + 128)).find("BUSY") != std::string::npos,
                     "req state 2 should map to BUSY (libpad scePadReqStateBusy)");

            setRegU32(ctx, 4, 1);
            setRegU32(ctx, 5, kPadDataAddr + 160);
            ps2_stubs::scePadReqIntToStr(rdram.data(), &ctx, nullptr);
            t.IsTrue(std::string(reinterpret_cast<const char *>(rdram.data() + kPadDataAddr + 160)).find("FAILED") != std::string::npos,
                     "req state 1 should map to FAILED (libpad scePadReqStateFaild)");
        });
        // Mirrors Stuntman sub_00126110 @0x00126110: the title's per-poll
        // handshake (digital open -> force analog -> actuators -> pressure)
        // must complete against these handlers, ending STABLE with a 0x73
        // buffer whose bytes move with (synthetic) host input.
        tc.Run("Stuntman handshake completes and buffer tracks synthetic input", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;
            auto call2 = [&](auto fn, uint32_t a2 = 0, uint32_t a3 = 0) -> uint32_t {
                setRegU32(ctx, 4, 0u);
                setRegU32(ctx, 5, 0u);
                setRegU32(ctx, 6, a2);
                setRegU32(ctx, 7, a3);
                fn(rdram.data(), &ctx, nullptr);
                return getRegU32(&ctx, 2);
            };

            ps2_stubs::scePadInit(rdram.data(), &ctx, nullptr);
            openPadPort(ctx, rdram);
            // Guest polls once per frame: FINDPAD, FINDPAD, then STABLE.
            t.Equals(call2(ps2_stubs::scePadGetState), 1u, "frame 1 after open should be FINDPAD (1)");
            t.Equals(call2(ps2_stubs::scePadGetState), 1u, "frame 2 after open should be FINDPAD (1)");
            t.Equals(call2(ps2_stubs::scePadGetState), 6u, "frame 3 after open should be STABLE (6)");

            runPadRead(ctx, rdram);
            const uint8_t *data = rdram.data() + kPadDataAddr;
            t.Equals(data[0], static_cast<uint8_t>(0), "byte0 must be 0 (title treats non-zero as no pad)");
            t.Equals(data[1], static_cast<uint8_t>(0x41), "pad opens DIGITAL (0x41)");
            t.Equals(call2(ps2_stubs::scePadInfoMode, 1, 0), 4u, "CURID should be digital (4) at open");
            t.Equals(call2(ps2_stubs::scePadInfoMode, 2, 0), 4u, "CUREXID should track current mode at open");

            t.Equals(call2(ps2_stubs::scePadSetMainMode, 1, 3), 1u, "SetMainMode(analog,lock) should succeed");
            t.Equals(call2(ps2_stubs::scePadGetState), 5u, "mode request should report EXECCMD (5) while BUSY");
            t.Equals(call2(ps2_stubs::scePadGetReqState), 2u, "mode request should be BUSY (2), not instant COMPLETE");
            for (int frame = 0; frame < 8 && call2(ps2_stubs::scePadGetReqState) != 0u; ++frame)
                call2(ps2_stubs::scePadGetState); // one guest frame per poll
            t.Equals(call2(ps2_stubs::scePadGetReqState), 0u, "mode request should reach COMPLETE (0) across frames");
            t.Equals(call2(ps2_stubs::scePadGetState), 6u, "then STABLE (6)");
            t.Equals(call2(ps2_stubs::scePadInfoMode, 1, 0), 7u, "CURID should be DualShock (7) after SetMainMode");

            t.Equals(call2(ps2_stubs::scePadInfoAct, static_cast<uint32_t>(-1)), 2u, "InfoAct(-1) should report 2 actuators");
            t.Equals(call2(ps2_stubs::scePadSetActAlign, kPadDataAddr + 0x100u), 1u, "SetActAlign should succeed");
            t.Equals(call2(ps2_stubs::scePadGetReqState), 2u, "actuator request should start BUSY (2), not instant COMPLETE");
            for (int frame = 0; frame < 8 && call2(ps2_stubs::scePadGetReqState) != 0u; ++frame)
                call2(ps2_stubs::scePadGetState);
            t.Equals(call2(ps2_stubs::scePadGetReqState), 0u, "actuator request should reach COMPLETE (0) across frames");
            t.Equals(call2(ps2_stubs::scePadInfoPressMode), 1u, "pressure mode should be supported");
            t.Equals(call2(ps2_stubs::scePadEnterPressMode), 1u, "EnterPressMode should succeed");
            t.Equals(call2(ps2_stubs::scePadGetState), 5u, "press-mode request should report EXECCMD (5) while BUSY");
            t.Equals(call2(ps2_stubs::scePadGetReqState), 2u, "pressure request should start BUSY (2)");
            for (int frame = 0; frame < 8 && call2(ps2_stubs::scePadGetReqState) != 0u; ++frame)
                call2(ps2_stubs::scePadGetState);
            t.Equals(call2(ps2_stubs::scePadGetReqState), 0u, "pressure request should reach COMPLETE (0) across frames");
            t.Equals(call2(ps2_stubs::scePadInfoComb, static_cast<uint32_t>(-1), 0), 0u, "no combined modes");
            t.Equals(call2(ps2_stubs::scePadGetState), 6u, "then STABLE (6)");

            auto dump = [&](const char *label) {
                std::printf("[pad-harness] %s ret=1 state=%u bytes:", label, call2(ps2_stubs::scePadGetState));
                for (int i = 0; i < 32; ++i)
                    std::printf(" %02X", data[i]);
                std::printf("\n");
                std::fflush(stdout);
            };

            const uint16_t idle = 0xFFFFu;
            ps2_stubs::setPadOverrideState(idle, 0x80, 0x80, 0x80, 0x80);
            runPadRead(ctx, rdram);
            dump("idle            ");
            t.Equals(data[1], static_cast<uint8_t>(0x73), "analog buffer should be 0x73 after handshake");
            t.Equals(readButtons(rdram), idle, "idle buttons should be 0xFFFF (active-low)");

            const uint16_t crossStart = static_cast<uint16_t>(0xFFFFu & ~kPadBtnCross & ~kPadBtnStart);
            ps2_stubs::setPadOverrideState(crossStart, 0xFF, 0x80, 0x00, 0x80);
            runPadRead(ctx, rdram);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), 1u, "scePadRead should return 1");
            dump("Cross+Start R/L ");
            t.Equals(readButtons(rdram), crossStart, "Cross+Start should clear bits 14 and 3");
            t.Equals(data[4], static_cast<uint8_t>(0x00), "rx should be full-left from synthetic input");
            t.Equals(data[6], static_cast<uint8_t>(0xFF), "lx should be full-right from synthetic input");
            t.Equals(data[14], static_cast<uint8_t>(0xFF), "Cross pressure byte should be 0xFF in pressure mode");

            const uint16_t squareUp = static_cast<uint16_t>(0xFFFFu & ~kPadBtnSquare & ~kPadBtnUp);
            ps2_stubs::setPadOverrideState(squareUp, 0x80, 0x00, 0x80, 0xFF);
            runPadRead(ctx, rdram);
            dump("Square+Up sticks");
            t.Equals(readButtons(rdram), squareUp, "Square+Up should clear bits 15 and 4");
            t.Equals(data[5], static_cast<uint8_t>(0xFF), "ry should be full-down from synthetic input");
            t.Equals(data[7], static_cast<uint8_t>(0x00), "ly should be full-up from synthetic input");
            t.Equals(data[15], static_cast<uint8_t>(0xFF), "Square pressure byte should be 0xFF");
            t.Equals(data[14], static_cast<uint8_t>(0x00), "Cross pressure byte should clear again");

            ps2_stubs::clearPadOverrideState();
            closePadPort(ctx, rdram);
        });

        // Mirrors the guest pad+61 wait states in sub_126110 frame by
        // frame: each iteration below is one guest frame (one
        // scePadGetState poll + one scePadGetReqState check). The HLE must
        // NOT complete requests instantly, must report them BUSY (2) - the
        // guest's wait states 42/71/77 spin on BUSY and re-issue only on
        // FAILED (1) - and must reach COMPLETE, ending in the guest's type
        // 4 (DualShock 2 + pressure) steady state.
        tc.Run("Stuntman pad+61 wait states progress frame-by-frame to type 4", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;
            auto call2 = [&](auto fn, uint32_t a2 = 0, uint32_t a3 = 0) -> uint32_t {
                setRegU32(ctx, 4, 0u);
                setRegU32(ctx, 5, 0u);
                setRegU32(ctx, 6, a2);
                setRegU32(ctx, 7, a3);
                fn(rdram.data(), &ctx, nullptr);
                return getRegU32(&ctx, 2);
            };
            int frames = 0;
            auto frameState = [&]() -> uint32_t {
                ++frames;
                return call2(ps2_stubs::scePadGetState);
            };

            t.Equals(call2(ps2_stubs::scePadGetState), 0u, "closed port should be DISCONNECTED (0)");
            ps2_stubs::scePadInit(rdram.data(), &ctx, nullptr);
            openPadPort(ctx, rdram);

            // Connect: FINDPAD for two frames, STABLE on the third.
            t.Equals(frameState(), 1u, "connect frame 1 should be FINDPAD");
            runPadRead(ctx, rdram);
            t.Equals(frameState(), 1u, "connect frame 2 should be FINDPAD");
            t.Equals(frameState(), 6u, "connect frame 3 should be STABLE");
            runPadRead(ctx, rdram);
            const uint8_t *data = rdram.data() + kPadDataAddr;
            t.Equals(data[1], static_cast<uint8_t>(0x41), "digital mode byte before forcing analog");
            t.Equals(call2(ps2_stubs::scePadInfoMode, 1, 0), 4u, "CURID digital (guest state 0 -> 40)");
            t.Equals(call2(ps2_stubs::scePadInfoMode, 2, 0), 4u, "CUREXID digital (guest state 40 gate)");

            // Force analog (guest states 40-42): BUSY must be observed,
            // FAILED never, and COMPLETE must arrive within a few frames.
            t.Equals(call2(ps2_stubs::scePadSetMainMode, 1, 3), 1u, "SetMainMode should succeed");
            bool sawBusy = false;
            uint32_t req = 2u;
            for (int guard = 0; guard < 16 && req != 0u; ++guard)
            {
                frameState();
                req = call2(ps2_stubs::scePadGetReqState);
                t.IsTrue(req != 1u, "mode request must never report FAILED (1): guest state 42 would re-issue it");
                sawBusy = sawBusy || req == 2u;
            }
            t.IsTrue(sawBusy, "mode request must report BUSY (2) for at least one frame (not instant)");
            t.Equals(req, 0u, "mode request should COMPLETE across frames");
            t.Equals(call2(ps2_stubs::scePadInfoMode, 1, 0), 7u, "CURID DualShock after forcing analog");

            // Actuators (guest states 70-71), same wait pattern.
            t.Equals(call2(ps2_stubs::scePadInfoAct, static_cast<uint32_t>(-1)), 2u, "InfoAct(-1) should be 2");
            t.Equals(call2(ps2_stubs::scePadSetActAlign, kPadDataAddr + 0x100u), 1u, "SetActAlign should succeed");
            sawBusy = false;
            req = 2u;
            for (int guard = 0; guard < 16 && req != 0u; ++guard)
            {
                frameState();
                req = call2(ps2_stubs::scePadGetReqState);
                t.IsTrue(req != 1u, "actuator request must never report FAILED (1): guest state 71 would re-issue it");
                sawBusy = sawBusy || req == 2u;
            }
            t.IsTrue(sawBusy, "actuator request must report BUSY (2) for at least one frame");
            t.Equals(req, 0u, "actuator request should COMPLETE across frames");

            // Pressure (guest states 72-78), same wait pattern.
            t.Equals(call2(ps2_stubs::scePadInfoPressMode), 1u, "pressure should be supported (guest state 72 -> 76)");
            t.Equals(call2(ps2_stubs::scePadEnterPressMode), 1u, "EnterPressMode should succeed");
            sawBusy = false;
            req = 2u;
            for (int guard = 0; guard < 16 && req != 0u; ++guard)
            {
                frameState();
                req = call2(ps2_stubs::scePadGetReqState);
                t.IsTrue(req != 1u, "pressure request must never report FAILED (1): guest state 77 would re-issue it");
                sawBusy = sawBusy || req == 2u;
            }
            t.IsTrue(sawBusy, "pressure request must report BUSY (2) for at least one frame");
            t.Equals(req, 0u, "pressure request should COMPLETE across frames");
            t.Equals(call2(ps2_stubs::scePadInfoComb, static_cast<uint32_t>(-1), 0), 0u, "no combined modes (guest state 98)");

            // Steady state (guest state 99): STABLE, mode nibble from the
            // buffer equals CURID (7), pressure live -> guest type 4.
            t.Equals(frameState(), 6u, "steady state should be STABLE");
            const uint16_t cross = static_cast<uint16_t>(0xFFFFu & ~kPadBtnCross);
            ps2_stubs::setPadOverrideState(cross, 0x80, 0x80, 0x80, 0x80);
            runPadRead(ctx, rdram);
            t.Equals(data[1], static_cast<uint8_t>(0x73), "analog mode byte in steady state");
            t.Equals(static_cast<uint8_t>(data[1] >> 4), static_cast<uint8_t>(7), "buffer mode nibble should equal CURID 7");
            t.Equals(data[14], static_cast<uint8_t>(0xFF), "Cross pressure live -> guest reports type 4");
            t.IsTrue(frames >= 9, "handshake must span multiple frames, not complete instantly");

            ps2_stubs::clearPadOverrideState();
            closePadPort(ctx, rdram);
        });

        // The front end's own check. MAIN.FEP's update (sub_00110230) feeds
        // FELIB the "no controller" code 0x8000 - which draws "No controller
        // in controller port 1" - unless sub_00117B38 finds a pad: it asks
        // the pad manager for port 0's 8-byte record (sub_00122340) and
        // sub_001170B8 accepts it only when record+4 (the type sub_00126110
        // writes) is 4 (DualShock 2, pressure negotiated) or 8. Replays the
        // guest poll every frame, from scePadPortOpen through the 60 init
        // polls of sub_001220A8 and the menu's 1,500-frame idle window.
        tc.Run("Stuntman front end sees a DualShock 2 in controller port 1", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;
            ps2_stubs::scePadInit(rdram.data(), &ctx, nullptr);
            openPadPort(ctx, rdram);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), 1u, "scePadPortOpen(0, 0, dma) should succeed");

            StuntmanPadPoll guest{rdram, ctx};
            int firstConnected = -1;
            int framesDisconnectedAfter = 0;
            for (int frame = 0; frame < 60 + 1500; ++frame)
            {
                guest.poll();
                const bool connected = guest.recordType == 4u || guest.recordType == 8u; // sub_001170B8
                if (connected && firstConnected < 0)
                    firstConnected = frame;
                else if (!connected && firstConnected >= 0)
                    ++framesDisconnectedAfter;
            }
            t.IsTrue(firstConnected >= 0, "front end must see a controller in port 1 (record type 4)");
            t.IsTrue(firstConnected < 60, "controller must be negotiated within sub_001220A8's 60 init polls");
            t.Equals(framesDisconnectedAfter, 0, "controller must stay connected every frame once negotiated");
            t.Equals(guest.recordType, 4u, "record type: DualShock 2 with pressure");
            t.Equals(static_cast<uint32_t>(guest.state), 99u, "guest pad state machine should rest in steady state 99");
            t.Equals(static_cast<uint32_t>(guest.id), 7u, "negotiated id: DualShock (7)");
            t.Equals(guest.setMainModeCalls, 1, "scePadSetMainMode issued once (no FAILED retry)");
            t.Equals(guest.setActAlignCalls, 1, "scePadSetActAlign issued once (no 70<->71 retry loop)");
            t.Equals(guest.enterPressModeCalls, 1, "scePadEnterPressMode issued once (no FAILED retry)");

            closePadPort(ctx, rdram);
        });

        tc.Run("scePadGetFrameCount increments", [](TestCase &t)
               {
            R5900Context ctx;
            ps2_stubs::scePadGetFrameCount(nullptr, &ctx, nullptr);
            const uint32_t first = getRegU32(&ctx, 2);
            ps2_stubs::scePadGetFrameCount(nullptr, &ctx, nullptr);
            const uint32_t second = getRegU32(&ctx, 2);
            t.Equals(second, first + 1, "frame count should increment");
        });

        tc.Run("scePadStateIntToStr and scePadReqIntToStr write strings", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            setRegU32(ctx, 4, 6);
            setRegU32(ctx, 5, kPadDataAddr);
            ps2_stubs::scePadStateIntToStr(rdram.data(), &ctx, nullptr);
            const char *stateStr = reinterpret_cast<const char *>(rdram.data() + kPadDataAddr);
            t.IsTrue(std::string(stateStr).find("STABLE") != std::string::npos, "state string should include STABLE");

            setRegU32(ctx, 4, 0);
            setRegU32(ctx, 5, kPadDataAddr + 64);
            ps2_stubs::scePadReqIntToStr(rdram.data(), &ctx, nullptr);
            const char *reqStr = reinterpret_cast<const char *>(rdram.data() + kPadDataAddr + 64);
            t.IsTrue(std::string(reqStr).find("COMPLETE") != std::string::npos, "req string should include COMPLETE");
        });
    });
}
