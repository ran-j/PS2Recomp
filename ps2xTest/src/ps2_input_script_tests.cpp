// Vanguard M5: tests for the opt-in scripted input channel
// (PS2X_INPUT_SCRIPT) - parsing and timeline application are pure and
// raylib-free so they can be verified headless.

#include "MiniTest.h"
#include "runtime/ps2_input_script.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    constexpr uint16_t kCross = 0x4000u;
    constexpr uint16_t kStart = 0x0008u;

    std::string writeTempScript(const std::string &content)
    {
        const char *name = "ps2_input_script_test.txt";
        std::ofstream out(name, std::ios::binary | std::ios::trunc);
        out << content;
        return name;
    }
}

void register_ps2_input_script_tests()
{
    MiniTest::Case("InputScript", [](TestCase &tc)
                   {
        tc.Run("parser keeps lines, sorts by time, defaults sticks", [](TestCase &t)
               {
            const std::string path = writeTempScript(
                "t=500 btns=4000 lx=-128 ly=10 rx=3 ry=-4\n"
                "t=0 btns=0\n"
                "t=1200 btns=8 lx=127\n");
            std::vector<ps2x::PadScriptEvent> events;
            t.IsTrue(ps2x::LoadPadScript(path.c_str(), events), "script should load");
            t.Equals(events.size(), 3u, "three events");
            t.Equals(events[0].tMs, 0u, "sorted: neutral first");
            t.Equals(events[1].tMs, 500u, "sorted: cross second");
            t.Equals(events[2].tMs, 1200u, "sorted: start last");
            t.Equals(events[1].btns, kCross, "cross mask");
            t.Equals(events[2].btns, kStart, "start mask");
            t.Equals(events[1].lx, static_cast<uint8_t>(0), "-128 maps to 0x00");
            t.Equals(events[1].ly, static_cast<uint8_t>(138), "ly=10 maps to 138");
            t.Equals(events[1].rx, static_cast<uint8_t>(131), "rx=3 maps to 131");
            t.Equals(events[2].lx, static_cast<uint8_t>(255), "127 maps to 0xFF");
            t.Equals(events[2].ly, static_cast<uint8_t>(128), "missing ly defaults to center");
        });

        tc.Run("parser rejects missing and malformed files", [](TestCase &t)
               {
            std::vector<ps2x::PadScriptEvent> events;
            t.IsFalse(ps2x::LoadPadScript("ps2_input_script_missing_1234.txt", events), "missing file rejected");
            const std::string path = writeTempScript("garbage line\nalso garbage\n");
            t.IsFalse(ps2x::LoadPadScript(path.c_str(), events), "no valid lines rejected");
        });

        tc.Run("application holds last state at or after its time", [](TestCase &t)
               {
            std::vector<ps2x::PadScriptEvent> events{
                {0u, 0u, 128u, 128u, 128u, 128u},
                {500u, kCross, 100u, 128u, 128u, 128u},
                {1200u, static_cast<uint16_t>(kCross | kStart), 128u, 128u, 128u, 128u},
            };
            std::vector<uint8_t> data(32, 0);

            ps2x::ApplyPadScript(events, 0u, data.data());
            t.Equals(data[2], 0xFFu, "t=0: nothing pressed (low)");
            t.Equals(data[3], 0xFFu, "t=0: nothing pressed (high)");
            t.Equals(data[6], 128u, "t=0: stick centered");

            ps2x::ApplyPadScript(events, 499u, data.data());
            t.Equals(data[2], 0xFFu, "t=499: still neutral");
            ps2x::ApplyPadScript(events, 500u, data.data());
            // wire convention is active-low: pressed kCross (0x4000) -> high byte ~0x40
            t.Equals(data[3], static_cast<uint8_t>(0xFFu ^ (kCross >> 8)), "t=500: cross pressed (high)");
            t.Equals(data[6], 100u, "t=500: left stick x from script");

            ps2x::ApplyPadScript(events, 5000u, data.data());
            const uint16_t pressed = static_cast<uint16_t>(kCross | kStart);
            t.Equals(data[2], static_cast<uint8_t>(0xFFu ^ (pressed & 0xFF)), "t=5000: both pressed (low)");
            t.Equals(data[3], static_cast<uint8_t>(0xFFu ^ (pressed >> 8)), "t=5000: both pressed (high)");
        });
    });
}
