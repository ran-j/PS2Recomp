#include "MiniTest.h"
#include "ps2_runtime.h"
#include "ps2_syscalls.h"

#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>
#include <chrono>

using namespace ps2_syscalls;

namespace
{
    void setRegU32(R5900Context &ctx, int reg, uint32_t value)
    {
        ctx.r[reg] = _mm_set_epi64x(0, static_cast<int64_t>(value));
    }

    void writeGuestString(uint8_t *rdram, uint32_t addr, const std::string &value)
    {
        std::memcpy(rdram + addr, value.c_str(), value.size() + 1);
    }

    struct TempPaths
    {
        std::filesystem::path base;
        std::filesystem::path mcRoot;
        std::filesystem::path cdRoot;

        ~TempPaths()
        {
            std::error_code ec;
            std::filesystem::remove_all(base, ec);
        }
    };

    TempPaths makeTempPaths()
    {
        TempPaths paths;
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        paths.base = std::filesystem::temp_directory_path()
                   / ("ps2recomp-mc0-" + std::to_string(now));
        paths.mcRoot = paths.base / "mcroot";
        paths.cdRoot = paths.base / "cdroot";
        std::filesystem::create_directories(paths.mcRoot);
        std::filesystem::create_directories(paths.cdRoot);
        return paths;
    }
}

void register_ps2_runtime_io_tests()
{
    MiniTest::Case("PS2RuntimeIO", [](TestCase &tc)
                   {
        tc.Run("mc0 paths resolve to mcRoot for fioOpen/fioWrite", [](TestCase &t)
               {
            TempPaths paths = makeTempPaths();

            PS2Runtime::IoPaths ioPaths;
            ioPaths.elfDirectory = paths.cdRoot;
            ioPaths.hostRoot = paths.cdRoot;
            ioPaths.cdRoot = paths.cdRoot;
            ioPaths.mcRoot = paths.mcRoot;
            PS2Runtime::setIoPaths(ioPaths);

            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            const std::string dirPath = "mc0:/SAVEDATA";
            const std::string filePath = "mc0:/SAVEDATA/test.txt";
            const uint32_t dirAddr = 0x1000;
            const uint32_t fileAddr = 0x1100;
            writeGuestString(rdram.data(), dirAddr, dirPath);
            writeGuestString(rdram.data(), fileAddr, filePath);

            setRegU32(ctx, 4, dirAddr);
            fioMkdir(rdram.data(), &ctx, nullptr);
            const int32_t mkdirResult = static_cast<int32_t>(getRegU32(&ctx, 2));
            t.IsTrue(mkdirResult >= 0, "fioMkdir should succeed for mc0 directory");

            const uint32_t openFlags = PS2_FIO_O_WRONLY | PS2_FIO_O_CREAT | PS2_FIO_O_TRUNC;
            setRegU32(ctx, 4, fileAddr);
            setRegU32(ctx, 5, openFlags);
            fioOpen(rdram.data(), &ctx, nullptr);
            const int32_t fd = static_cast<int32_t>(getRegU32(&ctx, 2));
            t.IsTrue(fd >= 0, "fioOpen should return a valid file descriptor");

            const std::string payload = "hello mc0";
            const uint32_t bufAddr = 0x2000;
            std::memcpy(rdram.data() + bufAddr, payload.data(), payload.size());
            setRegU32(ctx, 4, static_cast<uint32_t>(fd));
            setRegU32(ctx, 5, bufAddr);
            setRegU32(ctx, 6, static_cast<uint32_t>(payload.size()));
            fioWrite(rdram.data(), &ctx, nullptr);
            const int32_t bytesWritten = static_cast<int32_t>(getRegU32(&ctx, 2));
            t.Equals(bytesWritten, static_cast<int32_t>(payload.size()), "fioWrite should write all bytes");

            setRegU32(ctx, 4, static_cast<uint32_t>(fd));
            fioClose(rdram.data(), &ctx, nullptr);

            const std::filesystem::path expected = paths.mcRoot / "SAVEDATA" / "test.txt";
            const std::filesystem::path unexpected = paths.cdRoot / "SAVEDATA" / "test.txt";

            t.IsTrue(std::filesystem::exists(expected), "mc0 file should exist under mcRoot");
            t.IsFalse(std::filesystem::exists(unexpected), "mc0 file should not be created under cdRoot");

            std::ifstream in(expected, std::ios::binary);
            std::string readback((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            t.Equals(readback, payload, "mc0 file content should match payload");
        });
    });
}
