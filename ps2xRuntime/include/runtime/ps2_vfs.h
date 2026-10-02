#pragma once

#include "ps2x/iop/ps2_path.h"

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class PS2RomDevice;

struct PS2VfsMounts
{
    std::filesystem::path hostRoot;
    std::filesystem::path cdRoot;
    std::filesystem::path memoryCard0Root;
};

struct PS2VfsStat
{
    bool directory = false;
    bool readOnly = false;
    uint64_t size = 0u;
    std::time_t created = 0;
    std::time_t accessed = 0;
    std::time_t modified = 0;
};

struct PS2VfsDescriptorInfo
{
    int32_t descriptor = -1;
    std::string device;
    std::string path;
};

class IPS2OpenFile
{
public:
    virtual ~IPS2OpenFile() = default;

    [[nodiscard]] virtual int64_t read(void *destination, size_t size) = 0;
    [[nodiscard]] virtual int64_t write(const void *source, size_t size) = 0;
    [[nodiscard]] virtual int64_t seek(int64_t offset, int whence) = 0;
};

class PS2Vfs
{
public:
    PS2Vfs() = default;
    ~PS2Vfs();

    PS2Vfs(const PS2Vfs &) = delete;
    PS2Vfs &operator=(const PS2Vfs &) = delete;

    [[nodiscard]] int32_t open(std::string_view path, uint32_t flags, const PS2VfsMounts &mounts, const PS2RomDevice &rom);
    [[nodiscard]] int32_t close(int32_t descriptor);
    [[nodiscard]] int64_t read(int32_t descriptor, void *destination, size_t size);
    [[nodiscard]] int64_t write(int32_t descriptor, const void *source, size_t size);
    [[nodiscard]] int64_t seek(int32_t descriptor, int64_t offset, int whence);

    [[nodiscard]] bool stat(std::string_view path, const PS2VfsMounts &mounts, const PS2RomDevice &rom, PS2VfsStat &result) const;
    [[nodiscard]] bool resolveHostPath(std::string_view path, const PS2VfsMounts &mounts, std::filesystem::path &result) const;
    [[nodiscard]] std::vector<PS2VfsDescriptorInfo> descriptors() const;

private:
    struct OpenDescriptor
    {
        std::unique_ptr<IPS2OpenFile> file;
        std::string device;
        std::string path;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<int32_t, OpenDescriptor> m_descriptors;
    int32_t m_nextDescriptor = 3;
};
