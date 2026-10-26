// Tests for the opt-in MKB mouse-look layer (PS2X_MKB).
// The config parser and the stick math (accumulate/decay/clamp, radial
// deadzone, invert_y) and the mouse-button mapping are pure and
// raylib-free so they can be verified headless.

#include "MiniTest.h"
#include "runtime/ps2_mkb.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

namespace
{
    constexpr uint16_t kCross = 0x4000u;
    constexpr uint16_t kL1 = 0x0400u;

    bool almostEqual(float a, float b, float eps = 1e-5f)
    {
        return std::fabs(a - b) <= eps;
    }

    std::string writeTempConfig(const std::string &content)
    {
        const char *name = "ps2_mkb_config_test.ini";
        std::ofstream out(name, std::ios::binary | std::ios::trunc);
        out << content;
        return name;
    }
}

void register_ps2_mkb_tests()
{
    MiniTest::Case("Mkb", [](TestCase &tc)
                   {
        tc.Run("config: missing file keeps defaults and returns false", [](TestCase &t)
               {
            ps2x::MkbConfig cfg;
            cfg.sens = 2.5f; // caller-preset values must survive a failed load
            t.IsFalse(ps2x::MkbLoadConfig("ps2_mkb_config_missing_1234.ini", cfg),
                      "missing file rejected");
            t.IsTrue(almostEqual(cfg.sens, 2.5f), "sens untouched");
            t.IsTrue(almostEqual(cfg.deadzone, 0.1f), "deadzone default");
            t.Equals(cfg.invertY, 0, "invert_y default");
            t.IsFalse(ps2x::MkbLoadConfig(nullptr, cfg), "null path rejected");
        });

        tc.Run("config: parses the [mkb] section, keeps defaults for absent keys", [](TestCase &t)
               {
            const std::string path = writeTempConfig(
                "# comment line\n"
                "[mkb]\n"
                "sens=2.5\n"
                "deadzone=0.25\n"
                "invert_y=1\n");
            ps2x::MkbConfig cfg;
            t.IsTrue(ps2x::MkbLoadConfig(path.c_str(), cfg), "section found");
            t.IsTrue(almostEqual(cfg.sens, 2.5f), "sens parsed");
            t.IsTrue(almostEqual(cfg.deadzone, 0.25f), "deadzone parsed");
            t.Equals(cfg.invertY, 1, "invert_y parsed");

            const std::string partial = writeTempConfig("[mkb]\nsens = 0.5 ; trailing junk\n");
            ps2x::MkbConfig partialCfg;
            t.IsTrue(ps2x::MkbLoadConfig(partial.c_str(), partialCfg), "partial section ok");
            t.IsTrue(almostEqual(partialCfg.sens, 0.5f), "whitespace-tolerant sens");
            t.IsTrue(almostEqual(partialCfg.deadzone, 0.1f), "absent deadzone keeps default");
            t.Equals(partialCfg.invertY, 0, "absent invert_y keeps default");
        });

        tc.Run("config: clamps deadzone, ignores other sections and junk", [](TestCase &t)
               {
            const std::string path = writeTempConfig(
                "[other]\nsens=9.0\n"
                "[mkb]\n"
                "deadzone=1.5\n"
                "garbage without equals\n"
                "sens=-3.0\n");
            ps2x::MkbConfig cfg;
            t.IsTrue(ps2x::MkbLoadConfig(path.c_str(), cfg), "section found");
            t.IsTrue(almostEqual(cfg.deadzone, 1.0f), "deadzone clamped to [0,1]");
            t.IsTrue(almostEqual(cfg.sens, 1.0f), "negative sens ignored, other section ignored");

            const std::string noSection = writeTempConfig("sens=7.0\ndeadzone=0.2\n");
            ps2x::MkbConfig noSectionCfg;
            t.IsFalse(ps2x::MkbLoadConfig(noSection.c_str(), noSectionCfg),
                      "file without [mkb] returns false");
            t.IsTrue(almostEqual(noSectionCfg.sens, 1.0f), "defaults survive");
        });

        tc.Run("delta accumulates scaled by sens and decays each frame", [](TestCase &t)
               {
            ps2x::MkbConfig cfg;
            cfg.sens = 0.05f;
            float rx = 0.0f, ry = 0.0f;

            ps2x::MkbApplyMouseDelta(rx, ry, 6.0f, -4.0f, cfg);
            t.IsTrue(almostEqual(rx, 0.24f), "frame 1: 6px * 0.05 accumulated then decayed");
            t.IsTrue(almostEqual(ry, -0.16f), "frame 1: -4px * 0.05 accumulated then decayed");

            ps2x::MkbApplyMouseDelta(rx, ry, 0.0f, 0.0f, cfg);
            t.IsTrue(almostEqual(rx, 0.192f), "frame 2: idle mouse decays toward center");
            t.IsTrue(almostEqual(ry, -0.128f), "frame 2: idle mouse decays toward center");

            ps2x::MkbApplyMouseDelta(rx, ry, 0.0f, 0.0f, cfg);
            t.IsTrue(almostEqual(rx, 0.1536f), "frame 3: keeps decaying");
        });

        tc.Run("delta clamps at full deflection before decaying", [](TestCase &t)
               {
            ps2x::MkbConfig cfg; // sens 1.0
            float rx = 0.0f, ry = 0.0f;
            ps2x::MkbApplyMouseDelta(rx, ry, 50.0f, -50.0f, cfg);
            t.IsTrue(almostEqual(rx, ps2x::kMkbStickDecayPerFrame),
                     "huge flick clamps to 1.0 then decays once");
            t.IsTrue(almostEqual(ry, -ps2x::kMkbStickDecayPerFrame), "same for Y");
        });

        tc.Run("invert_y flips only the vertical axis", [](TestCase &t)
               {
            ps2x::MkbConfig plain;
            ps2x::MkbConfig inverted;
            inverted.invertY = 1;
            float rx = 0.0f, ry = 0.0f;

            ps2x::MkbApplyMouseDelta(rx, ry, 1.0f, -1.0f, plain);
            t.IsTrue(almostEqual(rx, ps2x::kMkbStickDecayPerFrame), "X unaffected");
            t.IsTrue(almostEqual(ry, -ps2x::kMkbStickDecayPerFrame),
                     "mouse up = look up (stick up) by default");

            float rx2 = 0.0f, ry2 = 0.0f;
            ps2x::MkbApplyMouseDelta(rx2, ry2, 1.0f, -1.0f, inverted);
            t.IsTrue(almostEqual(rx2, ps2x::kMkbStickDecayPerFrame), "X still unaffected");
            t.IsTrue(almostEqual(ry2, ps2x::kMkbStickDecayPerFrame), "Y flipped");
        });

        tc.Run("stick bytes center inside the radial deadzone", [](TestCase &t)
               {
            ps2x::MkbConfig cfg; // deadzone 0.1
            uint8_t bx = 0, by = 0;

            ps2x::MkbStickToBytes(0.0f, 0.0f, cfg, bx, by);
            t.Equals(bx, 128u, "centered stick reads center byte");
            t.Equals(by, 128u, "centered stick reads center byte");

            ps2x::MkbStickToBytes(0.05f, 0.03f, cfg, bx, by);
            t.Equals(bx, 128u, "below-deadzone magnitude reads center (radial)");
            t.Equals(by, 128u, "below-deadzone magnitude reads center (radial)");

            ps2x::MkbConfig tight;
            tight.deadzone = 0.0f;
            ps2x::MkbStickToBytes(0.05f, 0.0f, tight, bx, by);
            t.Equals(bx, 134u, "zero deadzone maps small deflection linearly");
        });

        tc.Run("stick bytes rescale above the deadzone to full deflection", [](TestCase &t)
               {
            ps2x::MkbConfig cfg; // deadzone 0.1
            uint8_t bx = 0, by = 0;

            ps2x::MkbStickToBytes(1.0f, 0.0f, cfg, bx, by);
            t.Equals(bx, 255u, "full right = 0xFF");
            t.Equals(by, 128u, "Y centered");
            ps2x::MkbStickToBytes(-1.0f, 0.0f, cfg, bx, by);
            t.Equals(bx, 1u, "full left = 0x01 (128 - 127)");
            ps2x::MkbStickToBytes(0.0f, -1.0f, cfg, bx, by);
            t.Equals(bx, 128u, "X centered");
            t.Equals(by, 1u, "full up (mouse up) = 0x01");

            // rescale: magnitude in [dz..1] remapped to [0..1] with no jump
            ps2x::MkbConfig half;
            half.deadzone = 0.5f;
            ps2x::MkbStickToBytes(0.75f, 0.0f, half, bx, by);
            t.Equals(bx, 192u, "0.75 with dz 0.5 rescales to half deflection");
            ps2x::MkbStickToBytes(0.5f, 0.0f, half, bx, by);
            t.Equals(bx, 128u, "at the deadzone edge the stick reads center");
        });

        tc.Run("mouse buttons map LMB to CROSS and RMB to L1", [](TestCase &t)
               {
            t.Equals(ps2x::MkbMouseButtonsToPad(0u), 0u, "no button");
            t.Equals(ps2x::MkbMouseButtonsToPad(1u), kCross, "LMB = CROSS (fire)");
            t.Equals(ps2x::MkbMouseButtonsToPad(2u), kL1, "RMB = L1 (aim down sights)");
            t.Equals(ps2x::MkbMouseButtonsToPad(3u), static_cast<uint16_t>(kCross | kL1),
                     "both together");
            t.Equals(ps2x::MkbMouseButtonsToPad(4u), 0u, "middle button unmapped");
            t.Equals(ps2x::MkbMouseButtonsToPad(7u), static_cast<uint16_t>(kCross | kL1),
                     "extra buttons do not leak bits");
        });
    });
}
