#include "MiniTest.h"
#include "ps2_runtime.h"
#include "ps2_syscalls.h"
#include "ps2_stubs.h"

#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>
#include <chrono>

using namespace ps2_syscalls;

namespace
{
    // Guest memory address ranges for test data
    constexpr uint32_t GUEST_STRING_AREA_START = 0x1000;
    constexpr uint32_t GUEST_BUFFER_AREA_START = 0x2000;
    constexpr uint32_t GUEST_STACK_AREA_START = 0x6000;
    constexpr uint32_t GUEST_MC_SYNC_CMD_ADDR = GUEST_BUFFER_AREA_START + 0x1C00;
    constexpr uint32_t GUEST_MC_SYNC_RESULT_ADDR = GUEST_BUFFER_AREA_START + 0x1C04;
    constexpr uint32_t GUEST_MC_TABLE_ADDR = GUEST_BUFFER_AREA_START + 0x2000;
    constexpr uint32_t GUEST_MC2_SYNC_CMD_ADDR = GUEST_BUFFER_AREA_START + 0x1C08;
    constexpr uint32_t GUEST_MC2_SYNC_RESULT_ADDR = GUEST_BUFFER_AREA_START + 0x1C0C;
    constexpr uint32_t GUEST_MC2_ENTRY_ADDR = GUEST_BUFFER_AREA_START + 0x2400;
    
    // Common file I/O flag combinations
    constexpr uint32_t PS2_FIO_WRITE_CREATE_TRUNC = 
        PS2_FIO_O_WRONLY | PS2_FIO_O_CREAT | PS2_FIO_O_TRUNC;

    struct SceMcStDateTime
    {
        uint8_t resv2;
        uint8_t sec;
        uint8_t min;
        uint8_t hour;
        uint8_t day;
        uint8_t month;
        uint16_t year;
    };

    struct SceMcTblGetDir
    {
        SceMcStDateTime create;
        SceMcStDateTime modify;
        uint32_t fileSizeByte;
        uint16_t attrFile;
        uint16_t reserve1;
        uint32_t reserve2;
        uint32_t pdaAplNo;
        char entryName[32];
    };

    static_assert(sizeof(SceMcTblGetDir) == 64, "sceMcTblGetDir size mismatch");

    struct SceMc2DirEntry
    {
        SceMcStDateTime create;
        SceMcStDateTime modify;
        uint32_t fileSizeByte;
        uint16_t attrFile;
        uint16_t reserved;
        char entryName[32];
    };

    static_assert(sizeof(SceMc2DirEntry) == 56, "sceMc2DirEntry size mismatch");

    constexpr int32_t MC2_CMD_GETINFO = 2;
    constexpr int32_t MC2_CMD_FORMAT = 3;
    constexpr int32_t MC2_CMD_READ = 5;
    constexpr int32_t MC2_CMD_WRITE = 6;
    constexpr int32_t MC2_CMD_CREATEFILE = 7;
    constexpr int32_t MC2_CMD_MKDIR = 11;
    constexpr int32_t MC2_CMD_SEARCHFILE = 14;

    constexpr int32_t MC2_ERR_NOENTRY = static_cast<int32_t>(0x81010002u);
    constexpr int32_t MC2_ERR_EXISTS = static_cast<int32_t>(0x81010011u);

    void setRegU32(R5900Context &ctx, int reg, uint32_t value)
    {
        ctx.r[reg] = _mm_set_epi64x(0, static_cast<int64_t>(value));
    }

    int32_t getRegS32(const R5900Context *ctx, int reg)
    {
        return static_cast<int32_t>(::getRegU32(ctx, reg));
    }

    void writeGuestString(uint8_t *rdram, uint32_t addr, const std::string &value)
    {
        std::memcpy(rdram + addr, value.c_str(), value.size() + 1);
    }

    void writeGuestU32(uint8_t *rdram, uint32_t addr, uint32_t value)
    {
        std::memcpy(rdram + addr, &value, sizeof(value));
    }

    int32_t readGuestS32(const uint8_t *rdram, uint32_t addr)
    {
        int32_t value = 0;
        std::memcpy(&value, rdram + addr, sizeof(value));
        return value;
    }

    uint32_t readGuestU32(const uint8_t *rdram, uint32_t addr)
    {
        uint32_t value = 0;
        std::memcpy(&value, rdram + addr, sizeof(value));
        return value;
    }

    void clearContext(R5900Context &ctx)
    {
        std::memset(&ctx, 0, sizeof(ctx));
    }

    int32_t syncMc(std::vector<uint8_t> &rdram, int32_t *cmdOut = nullptr)
    {
        R5900Context syncCtx{};
        setRegU32(syncCtx, 4, 0u);
        setRegU32(syncCtx, 5, GUEST_MC_SYNC_CMD_ADDR);
        setRegU32(syncCtx, 6, GUEST_MC_SYNC_RESULT_ADDR);
        ps2_stubs::sceMcSync(rdram.data(), &syncCtx, nullptr);

        if (cmdOut)
        {
            *cmdOut = readGuestS32(rdram.data(), GUEST_MC_SYNC_CMD_ADDR);
        }
        return readGuestS32(rdram.data(), GUEST_MC_SYNC_RESULT_ADDR);
    }

    int32_t syncMc2(std::vector<uint8_t> &rdram, int32_t *cmdOut = nullptr)
    {
        R5900Context syncCtx{};
        setRegU32(syncCtx, 4, 0u);
        setRegU32(syncCtx, 5, GUEST_MC2_SYNC_CMD_ADDR);
        setRegU32(syncCtx, 6, GUEST_MC2_SYNC_RESULT_ADDR);
        ps2_stubs::sceMc2Sync2(rdram.data(), &syncCtx, nullptr);

        if (cmdOut)
        {
            *cmdOut = readGuestS32(rdram.data(), GUEST_MC2_SYNC_CMD_ADDR);
        }
        return readGuestS32(rdram.data(), GUEST_MC2_SYNC_RESULT_ADDR);
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

    struct TestContext
    {
        TempPaths paths;
        std::vector<uint8_t> rdram;
        R5900Context ctx;

        TestContext() : paths(makeTempPaths()), rdram(PS2_RAM_SIZE, 0)
        {
            PS2Runtime::IoPaths ioPaths;
            ioPaths.elfDirectory = paths.cdRoot;
            ioPaths.hostRoot = paths.cdRoot;
            ioPaths.cdRoot = paths.cdRoot;
            ioPaths.mcRoot = paths.mcRoot;
            PS2Runtime::setIoPaths(ioPaths);
        }
    };
}

void register_ps2_runtime_io_tests()
{
    MiniTest::Case("PS2RuntimeIO", [](TestCase &tc)
    {
        tc.Run("mc0 directory creation", [](TestCase &t)
        {
            TestContext test;

            const std::string dirPath = "mc0:/SAVEDATA";
            const uint32_t dirAddr = GUEST_STRING_AREA_START;
            writeGuestString(test.rdram.data(), dirAddr, dirPath);

            setRegU32(test.ctx, 4, dirAddr);
            fioMkdir(test.rdram.data(), &test.ctx, nullptr);
            
            const int32_t result = getRegS32(&test.ctx, 2);
            t.IsTrue(result >= 0, "fioMkdir should succeed for mc0: directory");

            const std::filesystem::path expected = test.paths.mcRoot / "SAVEDATA";
            t.IsTrue(std::filesystem::exists(expected), 
                "Directory should exist under mcRoot");
            t.IsTrue(std::filesystem::is_directory(expected), 
                "Created path should be a directory");
        });

        tc.Run("mc0 file write operations", [](TestCase &t)
        {
            TestContext test;

            // Setup: create directory first
            const std::string dirPath = "mc0:/SAVEDATA";
            const uint32_t dirAddr = GUEST_STRING_AREA_START;
            writeGuestString(test.rdram.data(), dirAddr, dirPath);
            setRegU32(test.ctx, 4, dirAddr);
            fioMkdir(test.rdram.data(), &test.ctx, nullptr);

            // Test: open file for writing
            const std::string filePath = "mc0:/SAVEDATA/test.txt";
            const uint32_t fileAddr = GUEST_STRING_AREA_START + 0x100;
            writeGuestString(test.rdram.data(), fileAddr, filePath);

            setRegU32(test.ctx, 4, fileAddr);
            setRegU32(test.ctx, 5, PS2_FIO_WRITE_CREATE_TRUNC);
            fioOpen(test.rdram.data(), &test.ctx, nullptr);
            
            const int32_t fd = getRegS32(&test.ctx, 2);
            t.IsTrue(fd >= 0, "fioOpen should return valid file descriptor");

            // Write payload
            const std::string payload = "hello mc0";
            const uint32_t bufAddr = GUEST_BUFFER_AREA_START;
            std::memcpy(test.rdram.data() + bufAddr, payload.data(), payload.size());

            setRegU32(test.ctx, 4, static_cast<uint32_t>(fd));
            setRegU32(test.ctx, 5, bufAddr);
            setRegU32(test.ctx, 6, static_cast<uint32_t>(payload.size()));
            fioWrite(test.rdram.data(), &test.ctx, nullptr);
            
            const int32_t bytesWritten = getRegS32(&test.ctx, 2);
            t.Equals(bytesWritten, static_cast<int32_t>(payload.size()), 
                "fioWrite should write all bytes");

            // Close file
            setRegU32(test.ctx, 4, static_cast<uint32_t>(fd));
            fioClose(test.rdram.data(), &test.ctx, nullptr);
            
            const int32_t closeResult = getRegS32(&test.ctx, 2);
            t.IsTrue(closeResult >= 0, "fioClose should succeed");

            // Verify on host filesystem
            const std::filesystem::path expectedPath = 
                test.paths.mcRoot / "SAVEDATA" / "test.txt";
            t.IsTrue(std::filesystem::exists(expectedPath), 
                "File should exist under mcRoot");

            std::ifstream in(expectedPath, std::ios::binary);
            std::string readback(
                (std::istreambuf_iterator<char>(in)), 
                std::istreambuf_iterator<char>());
            t.Equals(readback, payload, "File content should match written payload");
        });

        tc.Run("mc0 file read operations", [](TestCase &t)
        {
            TestContext test;

            // Setup: create directory and write file
            const std::string dirPath = "mc0:/SAVEDATA";
            const uint32_t dirAddr = GUEST_STRING_AREA_START;
            writeGuestString(test.rdram.data(), dirAddr, dirPath);
            setRegU32(test.ctx, 4, dirAddr);
            fioMkdir(test.rdram.data(), &test.ctx, nullptr);

            const std::string filePath = "mc0:/SAVEDATA/test.txt";
            const uint32_t fileAddr = GUEST_STRING_AREA_START + 0x100;
            writeGuestString(test.rdram.data(), fileAddr, filePath);

            // Write data
            const std::string payload = "hello mc0 read test";
            const uint32_t writeBufAddr = GUEST_BUFFER_AREA_START;
            std::memcpy(test.rdram.data() + writeBufAddr, payload.data(), payload.size());

            setRegU32(test.ctx, 4, fileAddr);
            setRegU32(test.ctx, 5, PS2_FIO_WRITE_CREATE_TRUNC);
            fioOpen(test.rdram.data(), &test.ctx, nullptr);
            int32_t fd = getRegS32(&test.ctx, 2);

            setRegU32(test.ctx, 4, static_cast<uint32_t>(fd));
            setRegU32(test.ctx, 5, writeBufAddr);
            setRegU32(test.ctx, 6, static_cast<uint32_t>(payload.size()));
            fioWrite(test.rdram.data(), &test.ctx, nullptr);

            setRegU32(test.ctx, 4, static_cast<uint32_t>(fd));
            fioClose(test.rdram.data(), &test.ctx, nullptr);

            // Test: read back via fioRead
            setRegU32(test.ctx, 4, fileAddr);
            setRegU32(test.ctx, 5, PS2_FIO_O_RDONLY);
            fioOpen(test.rdram.data(), &test.ctx, nullptr);
            fd = getRegS32(&test.ctx, 2);
            t.IsTrue(fd >= 0, "fioOpen for reading should succeed");

            // Read into different buffer area
            const uint32_t readBufAddr = GUEST_BUFFER_AREA_START + 0x1000;
            std::memset(test.rdram.data() + readBufAddr, 0, payload.size());

            setRegU32(test.ctx, 4, static_cast<uint32_t>(fd));
            setRegU32(test.ctx, 5, readBufAddr);
            setRegU32(test.ctx, 6, static_cast<uint32_t>(payload.size()));
            fioRead(test.rdram.data(), &test.ctx, nullptr);

            const int32_t bytesRead = getRegS32(&test.ctx, 2);
            t.Equals(bytesRead, static_cast<int32_t>(payload.size()), 
                "fioRead should read all bytes");

            std::string readback(
                reinterpret_cast<const char*>(test.rdram.data() + readBufAddr),
                payload.size()
            );
            t.Equals(readback, payload, "fioRead content should match original");

            setRegU32(test.ctx, 4, static_cast<uint32_t>(fd));
            fioClose(test.rdram.data(), &test.ctx, nullptr);
        });

        tc.Run("mc0 paths isolated from cdRoot", [](TestCase &t)
        {
            TestContext test;

            const std::string dirPath = "mc0:/ISOLATED";
            const std::string filePath = "mc0:/ISOLATED/test.txt";
            const uint32_t dirAddr = GUEST_STRING_AREA_START;
            const uint32_t fileAddr = GUEST_STRING_AREA_START + 0x100;
            
            writeGuestString(test.rdram.data(), dirAddr, dirPath);
            writeGuestString(test.rdram.data(), fileAddr, filePath);

            // Create directory and file on mc0:
            setRegU32(test.ctx, 4, dirAddr);
            fioMkdir(test.rdram.data(), &test.ctx, nullptr);

            setRegU32(test.ctx, 4, fileAddr);
            setRegU32(test.ctx, 5, PS2_FIO_WRITE_CREATE_TRUNC);
            fioOpen(test.rdram.data(), &test.ctx, nullptr);
            const int32_t fd = getRegS32(&test.ctx, 2);

            const std::string payload = "isolation test";
            const uint32_t bufAddr = GUEST_BUFFER_AREA_START;
            std::memcpy(test.rdram.data() + bufAddr, payload.data(), payload.size());

            setRegU32(test.ctx, 4, static_cast<uint32_t>(fd));
            setRegU32(test.ctx, 5, bufAddr);
            setRegU32(test.ctx, 6, static_cast<uint32_t>(payload.size()));
            fioWrite(test.rdram.data(), &test.ctx, nullptr);

            setRegU32(test.ctx, 4, static_cast<uint32_t>(fd));
            fioClose(test.rdram.data(), &test.ctx, nullptr);

            // Verify isolation
            const std::filesystem::path expectedMc = 
                test.paths.mcRoot / "ISOLATED" / "test.txt";
            const std::filesystem::path unexpectedCd = 
                test.paths.cdRoot / "ISOLATED" / "test.txt";

            t.IsTrue(std::filesystem::exists(expectedMc), 
                "mc0: file should exist under mcRoot");
            t.IsFalse(std::filesystem::exists(unexpectedCd), 
                "mc0: file should NOT exist under cdRoot");

            // Verify mcRoot directory structure
            t.IsTrue(std::filesystem::exists(test.paths.mcRoot / "ISOLATED"), 
                "mc0: directory should exist under mcRoot");
            t.IsFalse(std::filesystem::exists(test.paths.cdRoot / "ISOLATED"), 
                "mc0: directory should NOT exist under cdRoot");
        });

        tc.Run("sceMc open write read and close roundtrip through sync", [](TestCase &t)
        {
            TestContext test;

            const uint32_t dirAddr = GUEST_STRING_AREA_START + 0x400;
            const uint32_t fileAddr = GUEST_STRING_AREA_START + 0x500;
            const uint32_t writeBufAddr = GUEST_BUFFER_AREA_START + 0x300;
            const uint32_t readBufAddr = GUEST_BUFFER_AREA_START + 0x500;
            const std::string payload = "libmc roundtrip";

            writeGuestString(test.rdram.data(), dirAddr, "/SAVEDATA");
            writeGuestString(test.rdram.data(), fileAddr, "/SAVEDATA/test.bin");
            std::memcpy(test.rdram.data() + writeBufAddr, payload.data(), payload.size());

            clearContext(test.ctx);
            setRegU32(test.ctx, 4, 0u);
            setRegU32(test.ctx, 5, 0u);
            setRegU32(test.ctx, 6, dirAddr);
            ps2_stubs::sceMcMkdir(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(getRegS32(&test.ctx, 2), 0, "sceMcMkdir should dispatch successfully");

            int32_t cmd = 0;
            t.Equals(syncMc(test.rdram, &cmd), 0, "sceMcMkdir should finish successfully");
            t.Equals(cmd, 0x0B, "sceMcSync should report MKDIR as the last command");

            clearContext(test.ctx);
            setRegU32(test.ctx, 4, 0u);
            setRegU32(test.ctx, 5, 0u);
            setRegU32(test.ctx, 6, fileAddr);
            setRegU32(test.ctx, 7, PS2_FIO_O_RDWR | PS2_FIO_O_CREAT | PS2_FIO_O_TRUNC);
            ps2_stubs::sceMcOpen(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(getRegS32(&test.ctx, 2), 0, "sceMcOpen should dispatch successfully");

            const int32_t fd = syncMc(test.rdram, &cmd);
            t.IsTrue(fd > 0, "sceMcOpen should produce a positive descriptor in sceMcSync");
            t.Equals(cmd, 0x02, "sceMcSync should report OPEN as the last command");

            clearContext(test.ctx);
            setRegU32(test.ctx, 4, static_cast<uint32_t>(fd));
            setRegU32(test.ctx, 5, writeBufAddr);
            setRegU32(test.ctx, 6, static_cast<uint32_t>(payload.size()));
            ps2_stubs::sceMcWrite(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(syncMc(test.rdram, &cmd), static_cast<int32_t>(payload.size()),
                     "sceMcWrite should report the full byte count via sceMcSync");
            t.Equals(cmd, 0x06, "sceMcSync should report WRITE as the last command");

            clearContext(test.ctx);
            setRegU32(test.ctx, 4, static_cast<uint32_t>(fd));
            setRegU32(test.ctx, 5, 0u);
            setRegU32(test.ctx, 6, PS2_FIO_SEEK_SET);
            ps2_stubs::sceMcSeek(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(syncMc(test.rdram, &cmd), 0, "sceMcSeek should rewind to offset zero");
            t.Equals(cmd, 0x04, "sceMcSync should report SEEK as the last command");

            std::memset(test.rdram.data() + readBufAddr, 0, payload.size());
            clearContext(test.ctx);
            setRegU32(test.ctx, 4, static_cast<uint32_t>(fd));
            setRegU32(test.ctx, 5, readBufAddr);
            setRegU32(test.ctx, 6, static_cast<uint32_t>(payload.size()));
            ps2_stubs::sceMcRead(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(syncMc(test.rdram, &cmd), static_cast<int32_t>(payload.size()),
                     "sceMcRead should report the full byte count via sceMcSync");
            t.Equals(cmd, 0x05, "sceMcSync should report READ as the last command");

            std::string readback(reinterpret_cast<const char *>(test.rdram.data() + readBufAddr), payload.size());
            t.Equals(readback, payload, "sceMcRead should fill the guest buffer with the written payload");

            clearContext(test.ctx);
            setRegU32(test.ctx, 4, static_cast<uint32_t>(fd));
            ps2_stubs::sceMcClose(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(syncMc(test.rdram, &cmd), 0, "sceMcClose should finish successfully");
            t.Equals(cmd, 0x03, "sceMcSync should report CLOSE as the last command");

            const std::filesystem::path hostPath = test.paths.mcRoot / "SAVEDATA" / "test.bin";
            t.IsTrue(std::filesystem::exists(hostPath), "sceMcOpen/sceMcWrite should create the host file under mcRoot");
        });

        tc.Run("sceMcGetDir includes dot entries and file metadata", [](TestCase &t)
        {
            TestContext test;

            std::filesystem::create_directories(test.paths.mcRoot / "SAVEDATA");
            const std::string hostPayload = "abc123";
            {
                std::ofstream out(test.paths.mcRoot / "SAVEDATA" / "game.dat", std::ios::binary);
                out.write(hostPayload.data(), static_cast<std::streamsize>(hostPayload.size()));
            }

            const uint32_t patternAddr = GUEST_STRING_AREA_START + 0x700;
            writeGuestString(test.rdram.data(), patternAddr, "/SAVEDATA/*");

            clearContext(test.ctx);
            setRegU32(test.ctx, 4, 0u);
            setRegU32(test.ctx, 5, 0u);
            setRegU32(test.ctx, 6, patternAddr);
            setRegU32(test.ctx, 7, 0u);
            // EE n32 ABI: arguments 5 and 6 travel in $t0/$t1
            setRegU32(test.ctx, 8, 8u);
            setRegU32(test.ctx, 9, GUEST_MC_TABLE_ADDR);

            ps2_stubs::sceMcGetDir(test.rdram.data(), &test.ctx, nullptr);

            int32_t cmd = 0;
            const int32_t entryCount = syncMc(test.rdram, &cmd);
            t.Equals(cmd, 0x0D, "sceMcSync should report GETDIR as the last command");
            t.Equals(entryCount, 3, "sceMcGetDir should return '.', '..', and the matching file");

            const auto *entries = reinterpret_cast<const SceMcTblGetDir *>(test.rdram.data() + GUEST_MC_TABLE_ADDR);
            t.Equals(std::string(entries[0].entryName), std::string("."), "sceMcGetDir should return '.' first");
            t.Equals(std::string(entries[1].entryName), std::string(".."), "sceMcGetDir should return '..' second");
            t.Equals(std::string(entries[2].entryName), std::string("game.dat"), "sceMcGetDir should include the matching file entry");
            t.Equals(entries[2].fileSizeByte, static_cast<uint32_t>(hostPayload.size()),
                     "sceMcGetDir should report the host file size");
            t.IsTrue((entries[2].attrFile & 0x0080u) != 0u,
                     "sceMcGetDir file entries should carry the closed-file attribute");
        });

        tc.Run("sceMcGetInfo reports formatted and unformatted states", [](TestCase &t)
        {
            TestContext test;

            constexpr uint32_t typeAddr = GUEST_BUFFER_AREA_START + 0x900;
            constexpr uint32_t freeAddr = GUEST_BUFFER_AREA_START + 0x904;
            constexpr uint32_t formatAddr = GUEST_BUFFER_AREA_START + 0x908;

            clearContext(test.ctx);
            setRegU32(test.ctx, 4, 0u);
            setRegU32(test.ctx, 5, 0u);
            setRegU32(test.ctx, 6, typeAddr);
            setRegU32(test.ctx, 7, freeAddr);
            // EE n32 ABI: the fifth argument travels in $t0
            setRegU32(test.ctx, 8, formatAddr);
            ps2_stubs::sceMcGetInfo(test.rdram.data(), &test.ctx, nullptr);

            int32_t cmd = 0;
            t.Equals(syncMc(test.rdram, &cmd), 0, "formatted cards should report success through sceMcSync");
            t.Equals(cmd, 0x01, "sceMcSync should report GETINFO as the last command");
            t.Equals(readGuestS32(test.rdram.data(), typeAddr), 2, "sceMcGetInfo should report a PS2 memory card");
            t.Equals(readGuestS32(test.rdram.data(), formatAddr), 1, "sceMcGetInfo should report a formatted card");

            clearContext(test.ctx);
            setRegU32(test.ctx, 4, 0u);
            setRegU32(test.ctx, 5, 0u);
            ps2_stubs::sceMcUnformat(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(syncMc(test.rdram, &cmd), 0, "sceMcUnformat should complete successfully");

            clearContext(test.ctx);
            setRegU32(test.ctx, 4, 0u);
            setRegU32(test.ctx, 5, 0u);
            setRegU32(test.ctx, 6, typeAddr);
            setRegU32(test.ctx, 7, freeAddr);
            setRegU32(test.ctx, 8, formatAddr);
            ps2_stubs::sceMcGetInfo(test.rdram.data(), &test.ctx, nullptr);

            t.Equals(syncMc(test.rdram, &cmd), -2, "unformatted cards should report sceMcResNoFormat through sceMcSync");
            t.Equals(readGuestS32(test.rdram.data(), formatAddr), 0, "sceMcGetInfo should report an unformatted card after sceMcUnformat");
        });

        tc.Run("sceMcEnd resets libmc state so sync reports no active command", [](TestCase &t)
        {
            TestContext test;

            constexpr uint32_t dirAddr = GUEST_STRING_AREA_START + 0xB00;

            clearContext(test.ctx);
            ps2_stubs::sceMcInit(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(getRegS32(&test.ctx, 2), 0, "sceMcInit should succeed");

            writeGuestString(test.rdram.data(), dirAddr, "/SAVEDATA");
            clearContext(test.ctx);
            setRegU32(test.ctx, 4, 0u);
            setRegU32(test.ctx, 5, 0u);
            setRegU32(test.ctx, 6, dirAddr);
            ps2_stubs::sceMcMkdir(test.rdram.data(), &test.ctx, nullptr);
            int32_t cmd = 0;
            t.Equals(syncMc(test.rdram, &cmd), 0, "sceMcMkdir should complete before teardown");
            t.Equals(cmd, 0x0B, "sceMcSync should report MKDIR before teardown");

            clearContext(test.ctx);
            ps2_stubs::sceMcEnd(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(getRegS32(&test.ctx, 2), 0, "sceMcEnd should succeed");

            // libmc semantics: with no async command pending, sceMcSync returns -1
            // and leaves the cmd/result out-parameters untouched.
            R5900Context syncCtx{};
            setRegU32(syncCtx, 4, 0u);
            setRegU32(syncCtx, 5, GUEST_MC_SYNC_CMD_ADDR);
            setRegU32(syncCtx, 6, GUEST_MC_SYNC_RESULT_ADDR);
            ps2_stubs::sceMcSync(test.rdram.data(), &syncCtx, nullptr);
            t.Equals(getRegS32(&syncCtx, 2), -1,
                     "sceMcSync after sceMcEnd should report that no command is active");
        });

        tc.Run("sceMc2 mkdir create write read roundtrip through sync2", [](TestCase &t)
        {
            TestContext test;

            const uint32_t dirAddr = GUEST_STRING_AREA_START + 0xC00;
            const uint32_t fileAddr = GUEST_STRING_AREA_START + 0xD00;
            const uint32_t writeBufAddr = GUEST_BUFFER_AREA_START + 0x700;
            const uint32_t readBufAddr = GUEST_BUFFER_AREA_START + 0x800;
            const std::string payload = "libmc2 roundtrip";

            writeGuestString(test.rdram.data(), dirAddr, "/SAVEDATA");
            writeGuestString(test.rdram.data(), fileAddr, "/SAVEDATA/test.bin");
            std::memcpy(test.rdram.data() + writeBufAddr, payload.data(), payload.size());

            clearContext(test.ctx);
            ps2_stubs::sceMc2Init(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(getRegS32(&test.ctx, 2), 0, "sceMc2Init should succeed");

            clearContext(test.ctx);
            ps2_stubs::sceMc2CreateSocket(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(getRegS32(&test.ctx, 2), 0, "sceMc2CreateSocket should return the port 0 socket");

            clearContext(test.ctx);
            setRegU32(test.ctx, 5, dirAddr);
            ps2_stubs::sceMc2MkdirAsync(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(getRegS32(&test.ctx, 2), 0, "sceMc2MkdirAsync should accept the request");

            int32_t cmd = 0;
            t.Equals(syncMc2(test.rdram, &cmd), 0, "sceMc2MkdirAsync should finish successfully");
            t.Equals(cmd, MC2_CMD_MKDIR, "sceMc2Sync2 should report MKDIR as the finished command");

            clearContext(test.ctx);
            setRegU32(test.ctx, 5, fileAddr);
            ps2_stubs::sceMc2CreateFileAsync(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(syncMc2(test.rdram, &cmd), 0, "sceMc2CreateFileAsync should finish successfully");
            t.Equals(cmd, MC2_CMD_CREATEFILE, "sceMc2Sync2 should report CREATEFILE as the finished command");

            clearContext(test.ctx);
            setRegU32(test.ctx, 5, fileAddr);
            setRegU32(test.ctx, 6, writeBufAddr);
            setRegU32(test.ctx, 7, 0u);
            setRegU32(test.ctx, 8, static_cast<uint32_t>(payload.size()));
            ps2_stubs::sceMc2WriteFileAsync(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(syncMc2(test.rdram, &cmd), static_cast<int32_t>(payload.size()),
                     "sceMc2WriteFileAsync should report the full byte count");
            t.Equals(cmd, MC2_CMD_WRITE, "sceMc2Sync2 should report WRITE as the finished command");

            std::memset(test.rdram.data() + readBufAddr, 0, payload.size());
            clearContext(test.ctx);
            setRegU32(test.ctx, 5, fileAddr);
            setRegU32(test.ctx, 6, readBufAddr);
            setRegU32(test.ctx, 7, 0u);
            setRegU32(test.ctx, 8, static_cast<uint32_t>(payload.size()));
            ps2_stubs::sceMc2ReadFileAsync(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(syncMc2(test.rdram, &cmd), static_cast<int32_t>(payload.size()),
                     "sceMc2ReadFileAsync should report the full byte count");
            t.Equals(cmd, MC2_CMD_READ, "sceMc2Sync2 should report READ as the finished command");

            std::string readback(reinterpret_cast<const char *>(test.rdram.data() + readBufAddr), payload.size());
            t.Equals(readback, payload, "sceMc2ReadFileAsync should fill the guest buffer with the written payload");

            const uint32_t tailSize = 4u;
            const uint32_t tailOffset = static_cast<uint32_t>(payload.size()) - tailSize;
            std::memset(test.rdram.data() + readBufAddr, 0, payload.size());
            clearContext(test.ctx);
            setRegU32(test.ctx, 5, fileAddr);
            setRegU32(test.ctx, 6, readBufAddr);
            setRegU32(test.ctx, 7, tailOffset);
            setRegU32(test.ctx, 8, tailSize);
            ps2_stubs::sceMc2ReadFileAsync(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(syncMc2(test.rdram, &cmd), static_cast<int32_t>(tailSize),
                     "sceMc2ReadFileAsync should report the byte count of a partial read");

            std::string tail(reinterpret_cast<const char *>(test.rdram.data() + readBufAddr), tailSize);
            t.Equals(tail, payload.substr(tailOffset),
                     "sceMc2ReadFileAsync should honour the offset argument");

            const std::filesystem::path hostPath = test.paths.mcRoot / "SAVEDATA" / "test.bin";
            t.IsTrue(std::filesystem::exists(hostPath), "sceMc2CreateFileAsync should create the host file under mcRoot");
        });

        tc.Run("sceMc2SearchFileAsync reports file metadata and missing entries", [](TestCase &t)
        {
            TestContext test;

            std::filesystem::create_directories(test.paths.mcRoot / "SAVEDATA");
            const std::string hostPayload = "abc123";
            {
                std::ofstream out(test.paths.mcRoot / "SAVEDATA" / "game.dat", std::ios::binary);
                out.write(hostPayload.data(), static_cast<std::streamsize>(hostPayload.size()));
            }

            const uint32_t pathAddr = GUEST_STRING_AREA_START + 0xE00;
            writeGuestString(test.rdram.data(), pathAddr, "/SAVEDATA/game.dat");

            clearContext(test.ctx);
            setRegU32(test.ctx, 5, pathAddr);
            setRegU32(test.ctx, 6, GUEST_MC2_ENTRY_ADDR);
            ps2_stubs::sceMc2SearchFileAsync(test.rdram.data(), &test.ctx, nullptr);

            int32_t cmd = 0;
            t.Equals(syncMc2(test.rdram, &cmd), 0, "sceMc2SearchFileAsync should find the existing file");
            t.Equals(cmd, MC2_CMD_SEARCHFILE, "sceMc2Sync2 should report SEARCHFILE as the finished command");

            const auto *entry = reinterpret_cast<const SceMc2DirEntry *>(test.rdram.data() + GUEST_MC2_ENTRY_ADDR);
            t.Equals(std::string(entry->entryName), std::string("game.dat"),
                     "sceMc2SearchFileAsync should report the host file name");
            t.Equals(entry->fileSizeByte, static_cast<uint32_t>(hostPayload.size()),
                     "sceMc2SearchFileAsync should report the host file size");
            t.IsTrue((entry->attrFile & 0x0010u) != 0u,
                     "sceMc2SearchFileAsync should mark a regular file");
            t.IsTrue((entry->attrFile & 0x0020u) == 0u,
                     "sceMc2SearchFileAsync should not mark a regular file as a subdirectory");

            writeGuestString(test.rdram.data(), pathAddr, "/SAVEDATA/missing.dat");
            clearContext(test.ctx);
            setRegU32(test.ctx, 5, pathAddr);
            setRegU32(test.ctx, 6, GUEST_MC2_ENTRY_ADDR);
            ps2_stubs::sceMc2SearchFileAsync(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(syncMc2(test.rdram, &cmd), MC2_ERR_NOENTRY,
                     "sceMc2SearchFileAsync should report ENOENT for a missing file");
        });

        tc.Run("sceMc2GetInfoAsync reports a formatted PS2 card", [](TestCase &t)
        {
            TestContext test;

            constexpr uint32_t infoAddr = GUEST_BUFFER_AREA_START + 0xA00;

            clearContext(test.ctx);
            setRegU32(test.ctx, 5, infoAddr);
            ps2_stubs::sceMc2GetInfoAsync(test.rdram.data(), &test.ctx, nullptr);

            int32_t cmd = 0;
            t.Equals(syncMc2(test.rdram, &cmd), 0, "sceMc2GetInfoAsync should finish successfully");
            t.Equals(cmd, MC2_CMD_GETINFO, "sceMc2Sync2 should report GETINFO as the finished command");
            t.Equals(readGuestS32(test.rdram.data(), infoAddr + 0), 2,
                     "sceMc2GetInfoAsync should report a PS2 memory card");
            t.Equals(readGuestS32(test.rdram.data(), infoAddr + 4), 1,
                     "sceMc2GetInfoAsync should report a formatted card");
            t.Equals(readGuestS32(test.rdram.data(), infoAddr + 8), 0x2000,
                     "sceMc2GetInfoAsync should report the free cluster count");
        });

        tc.Run("sceMc2CreateFileAsync rejects duplicates and missing parents", [](TestCase &t)
        {
            TestContext test;

            const uint32_t dirAddr = GUEST_STRING_AREA_START + 0xF00;
            const uint32_t fileAddr = GUEST_STRING_AREA_START + 0x1000;
            const uint32_t orphanAddr = GUEST_STRING_AREA_START + 0x1100;

            writeGuestString(test.rdram.data(), dirAddr, "/SAVEDATA");
            writeGuestString(test.rdram.data(), fileAddr, "/SAVEDATA/once.bin");
            writeGuestString(test.rdram.data(), orphanAddr, "/NOSUCHDIR/orphan.bin");

            clearContext(test.ctx);
            setRegU32(test.ctx, 5, dirAddr);
            ps2_stubs::sceMc2MkdirAsync(test.rdram.data(), &test.ctx, nullptr);
            int32_t cmd = 0;
            t.Equals(syncMc2(test.rdram, &cmd), 0, "sceMc2MkdirAsync should create the parent directory");

            clearContext(test.ctx);
            setRegU32(test.ctx, 5, dirAddr);
            ps2_stubs::sceMc2MkdirAsync(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(syncMc2(test.rdram, &cmd), MC2_ERR_EXISTS,
                     "sceMc2MkdirAsync should report EEXIST for an existing directory");

            clearContext(test.ctx);
            setRegU32(test.ctx, 5, fileAddr);
            ps2_stubs::sceMc2CreateFileAsync(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(syncMc2(test.rdram, &cmd), 0, "sceMc2CreateFileAsync should create a new file");

            clearContext(test.ctx);
            setRegU32(test.ctx, 5, fileAddr);
            ps2_stubs::sceMc2CreateFileAsync(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(syncMc2(test.rdram, &cmd), MC2_ERR_EXISTS,
                     "sceMc2CreateFileAsync should report EEXIST for an existing file");

            clearContext(test.ctx);
            setRegU32(test.ctx, 5, orphanAddr);
            ps2_stubs::sceMc2CreateFileAsync(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(syncMc2(test.rdram, &cmd), MC2_ERR_NOENTRY,
                     "sceMc2CreateFileAsync should report ENOENT when the parent directory is missing");
        });

        tc.Run("sceMc2FormatAsync wipes and recreates the card root", [](TestCase &t)
        {
            TestContext test;

            std::filesystem::create_directories(test.paths.mcRoot / "SAVEDATA");

            clearContext(test.ctx);
            ps2_stubs::sceMc2FormatAsync(test.rdram.data(), &test.ctx, nullptr);

            int32_t cmd = 0;
            t.Equals(syncMc2(test.rdram, &cmd), 0, "sceMc2FormatAsync should finish successfully");
            t.Equals(cmd, MC2_CMD_FORMAT, "sceMc2Sync2 should report FORMAT as the finished command");
            t.IsFalse(std::filesystem::exists(test.paths.mcRoot / "SAVEDATA"),
                      "sceMc2FormatAsync should remove the existing card contents");
            t.IsTrue(std::filesystem::exists(test.paths.mcRoot),
                     "sceMc2FormatAsync should recreate an empty card root");
        });

        tc.Run("sceMc2CheckAsync reports a finished request exactly once", [](TestCase &t)
        {
            TestContext test;

            const uint32_t dirAddr = GUEST_STRING_AREA_START + 0x1200;
            writeGuestString(test.rdram.data(), dirAddr, "/PENDING");

            clearContext(test.ctx);
            ps2_stubs::sceMc2Init(test.rdram.data(), &test.ctx, nullptr);

            clearContext(test.ctx);
            setRegU32(test.ctx, 4, GUEST_MC2_SYNC_CMD_ADDR);
            setRegU32(test.ctx, 5, GUEST_MC2_SYNC_RESULT_ADDR);
            ps2_stubs::sceMc2CheckAsync(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(getRegS32(&test.ctx, 2), -1,
                     "sceMc2CheckAsync after sceMc2Init should report that nothing is pending");

            clearContext(test.ctx);
            setRegU32(test.ctx, 5, dirAddr);
            ps2_stubs::sceMc2MkdirAsync(test.rdram.data(), &test.ctx, nullptr);

            clearContext(test.ctx);
            setRegU32(test.ctx, 4, GUEST_MC2_SYNC_CMD_ADDR);
            setRegU32(test.ctx, 5, GUEST_MC2_SYNC_RESULT_ADDR);
            ps2_stubs::sceMc2CheckAsync(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(getRegS32(&test.ctx, 2), 1, "sceMc2CheckAsync should report the finished request");
            t.Equals(readGuestS32(test.rdram.data(), GUEST_MC2_SYNC_CMD_ADDR), MC2_CMD_MKDIR,
                     "sceMc2CheckAsync should report MKDIR as the finished command");
            t.Equals(readGuestS32(test.rdram.data(), GUEST_MC2_SYNC_RESULT_ADDR), 0,
                     "sceMc2CheckAsync should report the mkdir result");

            clearContext(test.ctx);
            setRegU32(test.ctx, 4, GUEST_MC2_SYNC_CMD_ADDR);
            setRegU32(test.ctx, 5, GUEST_MC2_SYNC_RESULT_ADDR);
            ps2_stubs::sceMc2CheckAsync(test.rdram.data(), &test.ctx, nullptr);
            t.Equals(getRegS32(&test.ctx, 2), -1,
                     "a second sceMc2CheckAsync should report that the request was already taken");
        });

        tc.Run("sceIoctl cmd1 updates wait flag state", [](TestCase &t)
        {
            TestContext test;

            constexpr uint32_t statusAddr = GUEST_BUFFER_AREA_START + 0x1800;
            const uint32_t busy = 1u;
            std::memcpy(test.rdram.data() + statusAddr, &busy, sizeof(busy));

            setRegU32(test.ctx, 4, 3u);          // fd
            setRegU32(test.ctx, 5, 1u);          // cmd
            setRegU32(test.ctx, 6, statusAddr);  // arg

            ps2_stubs::sceIoctl(test.rdram.data(), &test.ctx, nullptr);

            t.Equals(getRegS32(&test.ctx, 2), 0, "sceIoctl cmd1 should return success");

            uint32_t state = 0xFFFFFFFFu;
            std::memcpy(&state, test.rdram.data() + statusAddr, sizeof(state));
            t.Equals(state, 0u, "sceIoctl cmd1 should clear wait state from busy to ready");
        });

        tc.Run("sceCdSearchFile resolves movie filenames with zero-padded host leaf", [](TestCase &t)
        {
            TestContext test;

            std::filesystem::create_directories(test.paths.cdRoot / "movie");
            {
                std::ofstream out(test.paths.cdRoot / "movie" / "mv_016.pss", std::ios::binary);
                const std::string payload = "pss";
                out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            }

            constexpr uint32_t fileAddr = GUEST_BUFFER_AREA_START + 0x1A00;
            constexpr uint32_t pathAddr = GUEST_STRING_AREA_START + 0xA00;
            writeGuestString(test.rdram.data(), pathAddr, "\\MOVIE\\MV_16.PSS;1");

            clearContext(test.ctx);
            setRegU32(test.ctx, 4, fileAddr);
            setRegU32(test.ctx, 5, pathAddr);
            ps2_stubs::sceCdSearchFile(test.rdram.data(), &test.ctx, nullptr);

            t.Equals(getRegS32(&test.ctx, 2), 1, "sceCdSearchFile should resolve the extracted movie file");
            t.Equals(readGuestU32(test.rdram.data(), fileAddr + 4), 3u,
                     "sceCdSearchFile should report the host file size");
            t.IsTrue(readGuestU32(test.rdram.data(), fileAddr + 0) >= 0x00100000u,
                     "sceCdSearchFile should assign a pseudo LSN for the resolved host file");
        });

        tc.Run("sceCdRead reads from explicit cdImage path", [](TestCase &t)
        {
            TestContext test;

            constexpr uint32_t kSectorSize = 2048u;
            constexpr uint32_t bufAddr = GUEST_BUFFER_AREA_START + 0x1C80;
            const std::filesystem::path imagePath = test.paths.base / "disc.iso";
            {
                std::vector<uint8_t> sector(kSectorSize, 0);
                const char payload[] = "cd-image";
                std::memcpy(sector.data(), payload, sizeof(payload) - 1);

                std::ofstream out(imagePath, std::ios::binary);
                out.write(reinterpret_cast<const char *>(sector.data()),
                          static_cast<std::streamsize>(sector.size()));
            }

            PS2Runtime::IoPaths ioPaths;
            ioPaths.elfDirectory = test.paths.cdRoot;
            ioPaths.hostRoot = test.paths.cdRoot;
            ioPaths.cdRoot = test.paths.cdRoot;
            ioPaths.mcRoot = test.paths.mcRoot;
            ioPaths.cdImage = imagePath;
            PS2Runtime::setIoPaths(ioPaths);

            clearContext(test.ctx);
            setRegU32(test.ctx, 4, 0u);
            setRegU32(test.ctx, 5, 1u);
            setRegU32(test.ctx, 6, bufAddr);
            ps2_stubs::sceCdRead(test.rdram.data(), &test.ctx, nullptr);

            t.Equals(getRegS32(&test.ctx, 2), 1, "sceCdRead should succeed when cdImage is configured");
            t.Equals(std::memcmp(test.rdram.data() + bufAddr, "cd-image", 8), 0,
                     "sceCdRead should copy sector data from the configured image");
        });
    });
}
