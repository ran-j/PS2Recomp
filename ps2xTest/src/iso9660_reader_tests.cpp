#include "MiniTest.h"
#include "runtime/iso9660_reader.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
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
        cursor += writeDirectoryRecord(system + cursor, 22u, 7u, 0u,
                                       "SLES_548.19;1");
        cursor += writeDirectoryRecord(system + cursor, 22u, 7u, 0u,
                                       "SYSTEM.CNF;1");

        std::memcpy(image.data() + 22u * kSectorSize, "ELFTEST", 7u);
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
            t.Equals(boot.size, 7u, "boot ELF should report its directory-record size");

            ps2x::iso9660::DirectoryEntry recursive;
            t.IsTrue(image.findFileRecursive("SLES_548.19", recursive),
                     "recursive search should find the boot ELF below the root");
            t.Equals(recursive.extentLba, 22u,
                     "recursive search should return the same boot ELF extent");

            std::vector<uint8_t> bytes;
            t.IsTrue(image.readFileFully(boot, bytes), "the boot ELF contents should be readable");
            t.Equals(bytes.size(), static_cast<size_t>(7u), "the complete file size should be read");
            t.IsTrue(std::memcmp(bytes.data(), "ELFTEST", 7u) == 0,
                     "the file payload should match the bytes stored in the ISO");
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
