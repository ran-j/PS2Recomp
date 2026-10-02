#pragma once

#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/gs_frontend.h"

#include <cstdint>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace GSTest
{
    constexpr uint32_t kVramSize = 4u * 1024u * 1024u;
    constexpr uint32_t kOutputPage = 200u;
    constexpr uint32_t kRed = 0x800000F8u;
    constexpr uint32_t kGreen = 0x8000F800u;
    constexpr uint32_t kBlue = 0x80F80000u;

    inline void require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error(std::string(message));
    }

    inline void expectEqual(uint32_t actual, uint32_t expected, std::string_view message)
    {
        if (actual != expected)
        {
            std::ostringstream error;
            error << message << ": expected 0x" << std::hex << expected << ", got 0x" << actual;
            throw std::runtime_error(error.str());
        }
    }

    struct Test
    {
        std::string_view name;
        void (*run)();
    };

    inline int run(int argc, char** argv, std::initializer_list<Test> tests)
    {
        try
        {
            if (argc != 2)
                throw std::invalid_argument("Pass one test name; run the complete suite with CTest.");
            for (const Test& test : tests)
            {
                if (test.name == argv[1])
                {
                    test.run();
                    std::cout << "PASS " << test.name << '\n';
                    return 0;
                }
            }
            throw std::invalid_argument("Unknown test name: " + std::string(argv[1]));
        }
        catch (const std::exception& error)
        {
            std::cerr << "FAIL: " << error.what() << '\n';
            return 1;
        }
    }

    inline GSTex0Reg texture(uint8_t psm = GS_PSM_CT32, uint32_t base = 32u)
    {
        GSTex0Reg tex{};
        tex.tbp0 = base;
        tex.tbw = 2;
        tex.psm = psm;
        tex.tw = tex.th = 8;
        tex.tcc = tex.tfx = 1;
        tex.cbp = 128;
        tex.cpsm = GS_PSM_CT32;
        tex.cld = 1;
        return tex;
    }

    inline uint64_t encodeTex0(const GSTex0Reg& tex)
    {
        return uint64_t(tex.tbp0) | (uint64_t(tex.tbw) << 14) | (uint64_t(tex.psm) << 20) |
               (uint64_t(tex.tw) << 26) | (uint64_t(tex.th) << 30) | (uint64_t(tex.tcc) << 34) |
               (uint64_t(tex.tfx) << 35) | (uint64_t(tex.cbp) << 37) | (uint64_t(tex.cpsm) << 51) |
               (uint64_t(tex.csm) << 55) | (uint64_t(tex.csa) << 56) | (uint64_t(tex.cld) << 61);
    }

    inline GSPrimitiveBatch sprite(const GSTex0Reg& tex, uint32_t x, uint32_t y, bool linear = false)
    {
        GSPrimitiveBatch batch{};
        batch.vertexCount = 2;
        auto& state = batch.state;
        state.prim.type = GS_PRIM_SPRITE;
        state.prim.tme = state.prim.fst = true;
        state.context.frame.fbp = kOutputPage;
        state.context.frame.fbw = 1;
        state.context.zbuf.zmask = true;
        state.context.test = 1ull << 17;
        state.context.tex0 = tex;
        state.context.clamp = 5; // Clamp both axes.
        state.textureWidth = state.textureHeight = 256;
        state.texa.ta0 = state.texa.ta1 = 128;
        state.linearFilter = linear;
        for (auto& vertex : batch.vertices)
        {
            vertex.r = vertex.g = vertex.b = vertex.a = 128;
            vertex.u = static_cast<uint16_t>(x * 16u);
            vertex.v = static_cast<uint16_t>(y * 16u);
        }
        batch.vertices[1].x = batch.vertices[1].y = 1;
        return batch;
    }

    struct BackendFixture
    {
        std::vector<uint8_t> vram = std::vector<uint8_t>(kVramSize);
        GSCpuBackend backend;

        BackendFixture()
        {
            backend.Initialize(vram.data(), static_cast<uint32_t>(vram.size()));
        }

        uint32_t sample(const GSTex0Reg& tex, uint32_t x = 0, uint32_t y = 0, bool linear = false)
        {
            backend.Submit(sprite(tex, x, y, linear));
            return backend.ReadVram(GS_PSM_CT32, kOutputPage * 32u, 1u, 0u, 0u);
        }
    };

    struct FrontendFixture
    {
        std::vector<uint8_t> vram = std::vector<uint8_t>(kVramSize);
        GS gs;

        FrontendFixture()
        {
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);
            for (uint8_t context = 0; context < 2; ++context)
            {
                gs.writeRegister(context ? GS_REG_FRAME_2 : GS_REG_FRAME_1, kOutputPage | (1ull << 16));
                gs.writeRegister(context ? GS_REG_ZBUF_2 : GS_REG_ZBUF_1, 1ull << 32);
                gs.writeRegister(context ? GS_REG_SCISSOR_2 : GS_REG_SCISSOR_1, 0);
                gs.writeRegister(context ? GS_REG_TEST_2 : GS_REG_TEST_1, 0x30000);
                gs.writeRegister(context ? GS_REG_CLAMP_2 : GS_REG_CLAMP_1, 5);
            }
            gs.writeRegister(GS_REG_TEXA, 128ull | (128ull << 32));
        }

        void bind(const GSTex0Reg& tex, uint8_t context = 0, bool tex2 = false)
        {
            const uint8_t reg = tex2 ? (context ? GS_REG_TEX2_2 : GS_REG_TEX2_1)
                                     : (context ? GS_REG_TEX0_2 : GS_REG_TEX0_1);
            gs.writeRegister(reg, encodeTex0(tex));
        }

        void flush()
        {
            gs.writeRegister(GS_REG_TEXFLUSH, 0);
        }

        void index(const GSTex0Reg& tex, uint32_t value, uint32_t x = 0, uint32_t y = 0)
        {
            gs.WriteVram(tex.psm, tex.tbp0, tex.tbw, x, y, value);
        }

        void palette(const GSTex0Reg& tex, uint32_t entry, uint32_t value)
        {
            // CSM1 source layout; CSA selects the destination, not this source.
            const uint32_t position = (entry & ~0x18u) | ((entry & 8u) << 1u) | ((entry & 16u) >> 1u);
            gs.WriteVram(tex.cpsm, tex.cbp, 1, position & 15u, position >> 4u, value);
        }

        uint32_t sample(uint32_t x = 0, uint32_t y = 0, uint8_t context = 0)
        {
            gs.writeRegister(GS_REG_PRIM, GS_PRIM_SPRITE | (1ull << 4) | (1ull << 8) | (uint64_t(context) << 9));
            gs.writeRegister(GS_REG_RGBAQ, 0x80808080);
            const uint64_t uv = (uint64_t(y * 16u) << 16) | uint64_t(x * 16u);
            gs.writeRegister(GS_REG_UV, uv);
            gs.writeRegister(GS_REG_XYZ2, 0);
            gs.writeRegister(GS_REG_UV, uv);
            gs.writeRegister(GS_REG_XYZ2, 16ull | (16ull << 16));
            return gs.ReadVram(GS_PSM_CT32, kOutputPage * 32u, 1, 0, 0);
        }
    };
}
