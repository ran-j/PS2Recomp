#include "runtime/ps2_rom_device.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace
{
    std::mutex &profileMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    std::vector<PS2RomProfile> &profileRegistry()
    {
        static std::vector<PS2RomProfile> profiles;
        return profiles;
    }

    bool equalsIgnoreCaseAscii(std::string_view lhs, std::string_view rhs)
    {
        if (lhs.size() != rhs.size())
            return false;
        for (size_t i = 0; i < lhs.size(); ++i)
        {
            const auto left = static_cast<unsigned char>(lhs[i]);
            const auto right = static_cast<unsigned char>(rhs[i]);
            if (std::tolower(left) != std::tolower(right))
                return false;
        }
        return true;
    }

    int matchSpecificity(const ps2x::iop::GameMatcher &matcher, const ps2x::iop::GameIdentity &identity)
    {
        int specificity = 0;
        if (!matcher.elfName.empty())
        {
            if (!equalsIgnoreCaseAscii(matcher.elfName, identity.elfName))
                return -1;
            ++specificity;
        }
        if (matcher.entryPoint != 0u)
        {
            if (matcher.entryPoint != identity.entryPoint)
                return -1;
            ++specificity;
        }
        if (matcher.crc32 != 0u)
        {
            if (matcher.crc32 != identity.crc32)
                return -1;
            ++specificity;
        }
        return specificity;
    }
}

PS2RomDevice::PS2RomDevice()
{
    mountBaseProfile();
}

void PS2RomDevice::registerProfile(PS2RomProfile profile)
{
    std::lock_guard<std::mutex> lock(profileMutex());
    profileRegistry().push_back(std::move(profile));
}

bool PS2RomDevice::configure(const ps2x::iop::GameIdentity &identity, std::string *error)
{
    m_files.clear();
    m_activeProfile.clear();
    m_activeProvider.clear();
    mountBaseProfile();

    std::vector<PS2RomProfile> profiles;
    {
        std::lock_guard<std::mutex> lock(profileMutex());
        profiles = profileRegistry();
    }

    const PS2RomProfile *selected = nullptr;
    const PS2RomProfile *tie = nullptr;
    int selectedSpecificity = -1;
    for (const PS2RomProfile &profile : profiles)
    {
        const int specificity = matchSpecificity(profile.matcher, identity);
        if (specificity < 0)
            continue;
        if (specificity > selectedSpecificity)
        {
            selected = &profile;
            tie = nullptr;
            selectedSpecificity = specificity;
        }
        else if (specificity == selectedSpecificity && selected)
        {
            tie = &profile;
        }
    }

    if (selected && tie)
    {
        if (error)
        {
            *error = "ambiguous ROM profiles '" + selected->provider + ":" + selected->id + "' and '" + tie->provider + ":" + tie->id + "'";
        }
        return false;
    }

    if (selected)
    {
        mountFiles(selected->files);
        m_activeProfile = selected->id;
        m_activeProvider = selected->provider;
    }
    return true;
}

bool PS2RomDevice::readFile(std::string_view ps2Path, std::vector<uint8_t> &bytes) const
{
    const auto file = m_files.find(normalizePath(ps2Path));
    if (file == m_files.end())
    {
        bytes.clear();
        return false;
    }
    bytes = file->second;
    return true;
}

bool PS2RomDevice::fileSize(std::string_view ps2Path, uint64_t &size) const
{
    const auto file = m_files.find(normalizePath(ps2Path));
    if (file == m_files.end())
    {
        size = 0u;
        return false;
    }
    size = file->second.size();
    return true;
}

bool PS2RomDevice::contains(std::string_view ps2Path) const
{
    return m_files.contains(normalizePath(ps2Path));
}

std::string PS2RomDevice::normalizePath(std::string_view path)
{
    constexpr std::string_view prefix = "rom0:";
    if (path.size() >= prefix.size() && equalsIgnoreCaseAscii(path.substr(0, prefix.size()), prefix))
        path.remove_prefix(prefix.size());
    while (!path.empty() && (path.front() == '/' || path.front() == '\\'))
        path.remove_prefix(1u);

    std::string normalized(path);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char value)
                   { return static_cast<char>(std::tolower(value)); });
    return normalized;
}

void PS2RomDevice::mountBaseProfile()
{
    // TODO expose this to cmake
    constexpr char romVersion[] = "0200AC20040614";
    static_assert(sizeof(romVersion) - 1u == 14u);
    m_files[normalizePath("ROMVER")] = std::vector<uint8_t>(romVersion, romVersion + 14u);
}

void PS2RomDevice::mountFiles(const std::unordered_map<std::string, std::vector<uint8_t>> &files)
{
    for (const auto &[path, bytes] : files)
        m_files[normalizePath(path)] = bytes;
}
