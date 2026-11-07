#include "MiniTest.h"
#include "runtime/iso9660_reader.h"
#include "runtime/ps2_vfs.h"
#include "runtime/ps2_rom_device.h"
#include "ps2_runtime.h"
#include "ps2_iop_host.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    constexpr size_t kSectorSize = 2048u;

    void putLe16(uint8_t *out, uint16_t value)
    {
        out[0] = static_cast<uint8_t>(value);
        out[1] = static_cast<uint8_t>(value >> 8u);
    }

    void putBe16(uint8_t *out, uint16_t value)
    {
        out[0] = static_cast<uint8_t>(value >> 8u);
        out[1] = static_cast<uint8_t>(value);
    }

    void putLe32(uint8_t *out, uint32_t value)
    {
        out[0] = static_cast<uint8_t>(value);
        out[1] = static_cast<uint8_t>(value >> 8u);
        out[2] = static_cast<uint8_t>(value >> 16u);
        out[3] = static_cast<uint8_t>(value >> 24u);
    }

    void putBe32(uint8_t *out, uint32_t value)
    {
        out[0] = static_cast<uint8_t>(value >> 24u);
        out[1] = static_cast<uint8_t>(value >> 16u);
        out[2] = static_cast<uint8_t>(value >> 8u);
        out[3] = static_cast<uint8_t>(value);
    }

    size_t writeDirectoryRecord(uint8_t *out,
                                uint32_t lba,
                                uint32_t size,
                                uint8_t flags,
                                const std::string &name)
    {
        const size_t identifierLength = name.size();
        const size_t recordLength = 33u + identifierLength + ((identifierLength % 2u) == 0u ? 1u : 0u);
        std::memset(out, 0, recordLength);
        out[0] = static_cast<uint8_t>(recordLength);
        out[1] = 0u; // extended-attribute length
        putLe32(out + 2u, lba);
        putBe32(out + 6u, lba);
        putLe32(out + 10u, size);
        putBe32(out + 14u, size);
        out[25u] = flags;
        putLe16(out + 28u, 1u);
        putBe16(out + 30u, 1u);
        out[32u] = static_cast<uint8_t>(identifierLength);
        std::memcpy(out + 33u, name.data(), identifierLength);
        return recordLength;
    }

    std::vector<uint8_t> makeIsoFixture()
    {
        // Sectors 16/17: primary and terminator descriptors;
        // sectors 20/21: root and SYSTEM directories; sector 22: boot ELF bytes.
        std::vector<uint8_t> image(24u * kSectorSize, 0u);

        uint8_t *pvd = image.data() + 16u * kSectorSize;
        pvd[0] = 1u;
        std::memcpy(pvd + 1u, "CD001", 5u);
        pvd[6] = 1u;
        putLe32(pvd + 80u, 24u);
        putBe32(pvd + 84u, 24u);
        putLe16(pvd + 128u, static_cast<uint16_t>(kSectorSize));
        putBe16(pvd + 130u, static_cast<uint16_t>(kSectorSize));
        const size_t rootLength = writeDirectoryRecord(pvd + 156u, 20u,
                                                       static_cast<uint32_t>(kSectorSize),
                                                       0x02u, std::string(1u, '\0'));
        pvd[156u] = static_cast<uint8_t>(rootLength);

        uint8_t *terminator = image.data() + 17u * kSectorSize;
        terminator[0] = 255u;
        std::memcpy(terminator + 1u, "CD001", 5u);
        terminator[6] = 1u;

        uint8_t *root = image.data() + 20u * kSectorSize;
        size_t cursor = 0u;
        cursor += writeDirectoryRecord(root + cursor, 21u,
                                       static_cast<uint32_t>(kSectorSize),
                                       0x02u, "SYSTEM");
        cursor += writeDirectoryRecord(root + cursor, 22u, 7u, 0u,
                                       "ROOTFILE.BIN;1");

        uint8_t *system = image.data() + 21u * kSectorSize;
        cursor = 0u;
        const std::string systemConfig =
            "BOOT2 = cdrom0:\\SLES_548.19;1\nVER = 2.00\nVMODE = PAL\n";
        cursor += writeDirectoryRecord(system + cursor, 22u, 52u, 0u,
                                       "SLES_548.19;1");
        cursor += writeDirectoryRecord(system + cursor, 23u,
                                       static_cast<uint32_t>(systemConfig.size()), 0u,
                                       "SYSTEM.CNF;1");

        std::array<uint8_t, 52u> elfHeader{};
        elfHeader[0] = 0x7Fu;
        elfHeader[1] = 'E';
        elfHeader[2] = 'L';
        elfHeader[3] = 'F';
        elfHeader[4] = 1u; // ELFCLASS32
        elfHeader[5] = 1u; // little-endian
        elfHeader[6] = 1u; // ELF version
        elfHeader[16] = 2u; // ET_EXEC
        elfHeader[18] = 8u; // EM_MIPS
        elfHeader[20] = 1u;
        std::memcpy(image.data() + 22u * kSectorSize, elfHeader.data(), elfHeader.size());
        std::memcpy(image.data() + 23u * kSectorSize,
                    systemConfig.data(), systemConfig.size());
        return image;
    }

    struct TemporaryIso
    {
        std::filesystem::path directory;
        std::filesystem::path path;

        TemporaryIso()
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            directory = std::filesystem::temp_directory_path() /
                        ("ps2recomp-iso9660-" + std::to_string(stamp));
            std::filesystem::create_directories(directory);
            path = directory / "fixture.iso";
        }

        ~TemporaryIso()
        {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }

        bool write(const std::vector<uint8_t> &bytes) const
        {
            std::ofstream output(path, std::ios::binary);
            if (!output)
                return false;
            output.write(reinterpret_cast<const char *>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
            return output.good();
        }
    };
}

void register_iso9660_reader_tests()
{
    MiniTest::Case("ISO9660Reader", [](TestCase &tc)
    {
        tc.Run("opens a PS2-style ISO and finds the boot ELF by path and recursive search",
               [](TestCase &t)
        {
            TemporaryIso fixture;
            t.IsTrue(fixture.write(makeIsoFixture()), "the synthetic ISO fixture should be written");

            ps2x::iso9660::Image image;
            std::string error;
            t.IsTrue(image.open(fixture.path, &error),
                     "the ISO9660 primary volume descriptor and root record should be accepted");
            t.Equals(error, std::string(), "successful open should clear the error message");
            t.Equals(image.imageSize(), static_cast<uint64_t>(24u * kSectorSize),
                     "the reader should report the exact image size");

            ps2x::iso9660::DirectoryEntry boot;
            t.IsTrue(image.findFile("system/sles_548.19", boot),
                     "path lookup should ignore case and the ISO ;version suffix");
            t.Equals(boot.extentLba, 22u, "boot ELF should resolve to the expected sector");
            t.Equals(boot.size, 52u, "boot ELF should report its directory-record size");

            ps2x::iso9660::DirectoryEntry recursive;
            t.IsTrue(image.findFileRecursive("SLES_548.19", recursive),
                     "recursive search should find the boot ELF below the root");
            t.Equals(recursive.extentLba, 22u,
                     "recursive search should return the same boot ELF extent");

            std::vector<uint8_t> bytes;
            t.IsTrue(image.readFileFully(boot, bytes), "the boot ELF contents should be readable");
            t.Equals(bytes.size(), static_cast<size_t>(52u), "the complete file size should be read");
            t.IsTrue(std::memcmp(bytes.data(), "\x7F" "ELF", 4u) == 0,
                     "the boot file should expose a valid ELF magic signature");
        });

        tc.Run("runtime validates SYSTEM.CNF and the MIPS boot ELF before mounting",
               [](TestCase &t)
        {
            TemporaryIso fixture;
            t.IsTrue(fixture.write(makeIsoFixture()), "the synthetic ISO fixture should be written");

            const PS2Runtime::IoPaths previousPaths = PS2Runtime::getIoPaths();
            std::string error;
            const bool mounted = PS2Runtime::mountCdIsoImage(
                fixture.path, "SLES_548.19;1", &error);
            t.IsTrue(mounted,
                     "runtime should accept an ISO whose SYSTEM.CNF and ELF match the expected serial");
            t.Equals(error, std::string(), "successful mount should clear the error message");
            t.IsTrue(PS2Runtime::getIoPaths().cdIsoImage != nullptr,
                     "successful validation should publish the ISO mount to the runtime");
            t.Equals(PS2Runtime::getIoPaths().cdIsoImage->imageSize(),
                     static_cast<uint64_t>(24u * kSectorSize),
                     "the runtime should retain the mounted image");

            const std::filesystem::path extractedElf = fixture.directory / "SLES_548.19";
            t.IsTrue(PS2Runtime::extractMountedCdIsoBootElf(
                         "SLES_548.19;1", extractedElf, &error),
                     "runtime should extract only the boot ELF to the app-private boot path");
            t.IsTrue(std::filesystem::exists(extractedElf),
                     "the extracted boot ELF should be created");
            std::ifstream extracted(extractedElf, std::ios::binary);
            std::array<char, 4u> magic{};
            extracted.read(magic.data(), static_cast<std::streamsize>(magic.size()));
            t.IsTrue(std::memcmp(magic.data(), "\x7F" "ELF", magic.size()) == 0,
                     "the extracted file should preserve its PS2 ELF signature");
            PS2Runtime::setIoPaths(previousPaths);
        });

        tc.Run("VFS opens and reads files from a mounted CD-ROM ISO", [](TestCase &t)
        {
            TemporaryIso fixture;
            t.IsTrue(fixture.write(makeIsoFixture()), "the synthetic ISO fixture should be written");

            auto image = std::make_shared<ps2x::iso9660::Image>();
            std::string error;
            t.IsTrue(image->open(fixture.path, &error), "the image should mount before VFS use");

            PS2Vfs vfs;
            PS2RomDevice rom;
            PS2VfsMounts mounts;
            mounts.cdIsoImage = image;

            const int32_t descriptor = vfs.open("cdrom0:\\SYSTEM\\SLES_548.19;1",
                                                 1u, // PS2_FIO_O_RDONLY
                                                 mounts,
                                                 rom);
            t.IsTrue(descriptor >= 3, "VFS should open a PS2 CD-ROM file from the mounted ISO");

            std::array<char, 4u> bytes{};
            t.Equals(vfs.read(descriptor, bytes.data(), bytes.size()), int64_t{4},
                     "VFS should return the requested boot ELF header bytes");
            t.IsTrue(std::memcmp(bytes.data(), "\x7F" "ELF", bytes.size()) == 0,
                     "VFS should read the ELF signature directly from the ISO extent");

            t.Equals(vfs.seek(descriptor, 0, SEEK_SET), int64_t{0},
                     "VFS should seek to the beginning of an ISO-backed file");
            t.Equals(vfs.close(descriptor), 0, "VFS should close the ISO-backed descriptor");

            PS2VfsStat stat;
            t.IsTrue(vfs.stat("cdrom0:/SYSTEM/SLES_548.19;1", mounts, rom, stat),
                     "VFS stat should resolve an ISO-backed file");
            t.Equals(stat.size, uint64_t{52}, "VFS stat should expose the directory-record file size");
            t.IsTrue(stat.readOnly, "CD-ROM ISO entries must be read-only");
        });

        tc.Run("IOP host adapter opens ISO-backed cdrom0 paths", [](TestCase &t)
        {
            TemporaryIso fixture;
            t.IsTrue(fixture.write(makeIsoFixture()), "the synthetic ISO fixture should be written");

            auto image = std::make_shared<ps2x::iso9660::Image>();
            std::string error;
            t.IsTrue(image->open(fixture.path, &error), "the ISO should open for IOP adapter testing");

            const PS2Runtime::IoPaths previousPaths = PS2Runtime::getIoPaths();
            PS2Runtime::IoPaths paths = previousPaths;
            paths.cdImage = fixture.path;
            paths.cdIsoImage = image;
            PS2Runtime::setIoPaths(paths);

            PS2Runtime runtime;
            PS2IopHostAdapter host(runtime);
            const std::string translated = host.translateGuestPath(
                "cdrom0:\\SYSTEM\\SLES_548.19;1");
            t.IsTrue(translated.rfind("ps2x-iso://", 0u) == 0u,
                     "IOP path translation should return the ISO-backed handle path");

            const uint64_t handle = host.openHostFile(translated);
            t.IsTrue(handle != 0u, "IOP host should open the translated ISO file");
            uint64_t fileSize = 0u;
            t.IsTrue(host.hostFileSize(handle, fileSize),
                     "IOP host should report the ISO directory-record file size");
            t.Equals(fileSize, uint64_t{52}, "IOP host should report the boot ELF size");

            std::array<char, 4u> magic{};
            size_t bytesRead = 0u;
            t.IsTrue(host.readHostFile(handle, 0u, magic.data(), magic.size(), bytesRead),
                     "IOP host should read data from the ISO file extent");
            t.Equals(bytesRead, magic.size(), "IOP host should return the requested byte count");
            t.IsTrue(std::memcmp(magic.data(), "\x7F" "ELF", magic.size()) == 0,
                     "IOP host should return the PS2 ELF signature");
            host.closeHostFile(handle);

            PS2Runtime::setIoPaths(previousPaths);
        });

        tc.Run("rejects a file without an ISO9660 descriptor signature", [](TestCase &t)
        {
            TemporaryIso fixture;
            std::vector<uint8_t> invalid(24u * kSectorSize, 0u);
            t.IsTrue(fixture.write(invalid), "the invalid fixture should be written");

            ps2x::iso9660::Image image;
            std::string error;
            t.IsFalse(image.open(fixture.path, &error),
                      "a zero-filled file must not be accepted as an ISO9660 image");
            t.IsTrue(!error.empty(), "invalid image rejection should explain the failure");
        });
    });
}
