#include "ps2_syscalls.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_stubs.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <memory>
#include <string>

#ifndef _WIN32
#include <unistd.h>   // for unlink,rmdir,chdir
#include <sys/stat.h> // for mkdir
#endif
#include <ThreadNaming.h>

std::string translatePs2Path(const char *ps2Path);

namespace
{
    std::string toLowerAscii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    std::string stripIsoVersionSuffix(std::string value)
    {
        const std::size_t semicolon = value.find(';');
        if (semicolon == std::string::npos)
        {
            return value;
        }

        bool numericSuffix = semicolon + 1 < value.size();
        for (std::size_t i = semicolon + 1; i < value.size(); ++i)
        {
            if (!std::isdigit(static_cast<unsigned char>(value[i])))
            {
                numericSuffix = false;
                break;
            }
        }

        if (numericSuffix)
        {
            value.erase(semicolon);
        }
        return value;
    }

    std::string normalizePs2PathSuffix(std::string suffix)
    {
        std::replace(suffix.begin(), suffix.end(), '\\', '/');
        suffix = stripIsoVersionSuffix(std::move(suffix));
        while (!suffix.empty() && (suffix.front() == '/' || suffix.front() == '\\'))
        {
            suffix.erase(suffix.begin());
        }
        return suffix;
    }

    std::filesystem::path getConfiguredHostRoot()
    {
        const PS2Runtime::IoPaths &paths = PS2Runtime::getIoPaths();
        if (!paths.hostRoot.empty())
        {
            return paths.hostRoot;
        }
        if (!paths.elfDirectory.empty())
        {
            return paths.elfDirectory;
        }

        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        return ec ? std::filesystem::path(".") : cwd.lexically_normal();
    }

    std::filesystem::path getConfiguredCdRoot()
    {
        const PS2Runtime::IoPaths &paths = PS2Runtime::getIoPaths();
        if (!paths.cdRoot.empty())
        {
            return paths.cdRoot;
        }
        if (!paths.elfDirectory.empty())
        {
            return paths.elfDirectory;
        }

        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        return ec ? std::filesystem::path(".") : cwd.lexically_normal();
    }
}

std::unordered_map<int, FILE *> g_fileDescriptors;
int g_nextFd = 3; // Start after stdin, stdout, stderr

struct ThreadInfo
{
    uint32_t entry = 0;
    uint32_t stack = 0;
    uint32_t stackSize = 0;
    uint32_t gp = 0;
    uint32_t priority = 0;
    uint32_t attr = 0;
    uint32_t option = 0;
    uint32_t arg = 0;
    bool started = false;
    uint32_t tlsBase = 0;

    // Thread Status
    int status = 0x10; // THS_DORMANT
    int waitType = 0;  // TSW_NONE
    int waitId = 0;
    int wakeupCount = 0;
    int currentPriority = 0;
    int suspendCount = 0;

    std::mutex m;
    std::condition_variable cv;
    std::atomic<bool> forceRelease{false};
    std::atomic<bool> terminated{false};
};

// Thread status
#define THS_RUN 0x01
#define THS_READY 0x02
#define THS_WAIT 0x04
#define THS_SUSPEND 0x08
#define THS_WAITSUSPEND 0x0c
#define THS_DORMANT 0x10

// Thread WAIT Status
#define TSW_NONE 0
#define TSW_SLEEP 1
#define TSW_SEMA 2
#define TSW_EVENT 3

// Common kernel-like error codes used by thread/event/alarm syscalls.
constexpr int KE_OK = 0;
constexpr int KE_ERROR = -1;
constexpr int KE_ILLEGAL_MODE = -405;
constexpr int KE_ILLEGAL_THID = -406;
constexpr int KE_UNKNOWN_THID = -407;
constexpr int KE_UNKNOWN_SEMID = -408;
constexpr int KE_UNKNOWN_EVFID = -409;
constexpr int KE_DORMANT = -413;
constexpr int KE_NOT_WAIT = -416;
constexpr int KE_RELEASE_WAIT = -418;
constexpr int KE_SEMA_ZERO = -419;
constexpr int KE_EVF_COND = -421;
constexpr int KE_EVF_MULTI = -422;
constexpr int KE_EVF_ILPAT = -423;
constexpr int KE_WAIT_DELETE = -425;

// SIF RPC Structures
struct t_SifRpcHeader
{
    uint32_t pkt_addr; // void*
    uint32_t rpc_id;
    int sema_id;
    uint32_t mode;
};

struct t_SifRpcClientData
{
    t_SifRpcHeader hdr;
    uint32_t command;
    uint32_t buf;          // void*
    uint32_t cbuf;         // void*
    uint32_t end_function; // func ptr
    uint32_t end_param;    // void*
    uint32_t server;       // t_SifRpcServerData*
};

struct t_SifRpcServerData
{
    int sid;
    uint32_t func; // func ptr
    uint32_t buf;  // void*
    int size;
    uint32_t cfunc; // func ptr
    uint32_t cbuf;  // void*
    int size2;
    uint32_t client;   // t_SifRpcClientData*
    uint32_t pkt_addr; // void*
    int rpc_number;
    uint32_t recvbuf; // void*
    int rsize;
    int rmode;
    int rid;
    uint32_t link; // t_SifRpcServerData*
    uint32_t next; // t_SifRpcServerData*
    uint32_t base; // t_SifRpcDataQueue*
};

struct t_SifRpcDataQueue
{
    int thread_id;
    int active;
    uint32_t link;  // t_SifRpcServerData*
    uint32_t start; // t_SifRpcServerData*
    uint32_t end;   // t_SifRpcServerData*
    uint32_t next;  // t_SifRpcDataQueue*
};

struct ee_thread_status_t
{
    int status;           // 0x00
    uint32_t func;        // 0x04
    uint32_t stack;       // 0x08
    int stack_size;       // 0x0C
    uint32_t gp_reg;      // 0x10
    int initial_priority; // 0x14
    int current_priority; // 0x18
    uint32_t attr;        // 0x1C
    uint32_t option;      // 0x20
    uint32_t waitType;    // 0x24
    uint32_t waitId;      // 0x28
    uint32_t wakeupCount; // 0x2C
};

struct ee_sema_t
{
    int count;
    int max_count;
    int init_count;
    int wait_threads;
    uint32_t attr;
    uint32_t option;
};

struct SemaInfo
{
    int count = 0;
    int maxCount = 0;
    int initCount = 0;
    uint32_t attr = 0;
    uint32_t option = 0;
    int waiters = 0;
    std::mutex m;
    std::condition_variable cv;
};

struct EventFlagInfo
{
    uint32_t attr = 0;
    uint32_t option = 0;
    uint32_t initBits = 0;
    uint32_t bits = 0;
    int waiters = 0;
    bool deleted = false;
    std::mutex m;
    std::condition_variable cv;
};

struct AlarmInfo
{
    int id = 0;
    uint16_t ticks = 0;
    uint32_t handler = 0;
    uint32_t commonArg = 0;
    uint32_t gp = 0;
    uint32_t sp = 0;
    uint8_t *rdram = nullptr;
    PS2Runtime *runtime = nullptr;
    std::chrono::steady_clock::time_point dueAt;
};

struct io_stat_t
{
    uint32_t mode;
    uint32_t attr;
    uint32_t size;
    uint8_t ctime[8];
    uint8_t atime[8];
    uint8_t mtime[8];
    uint32_t hisize;
};

static constexpr uint32_t kFioSoIfLnk = 0x0008;
static constexpr uint32_t kFioSoIfReg = 0x0010;
static constexpr uint32_t kFioSoIfDir = 0x0020;
static constexpr uint32_t kFioSoIROth = 0x0004;
static constexpr uint32_t kFioSoIWOth = 0x0002;
static constexpr uint32_t kFioSoIXOth = 0x0001;

static std::unordered_map<int, std::shared_ptr<ThreadInfo>> g_threads;
static int g_nextThreadId = 2; // Reserve 1 for the main thread
static thread_local int g_currentThreadId = 1;
static std::mutex g_thread_map_mutex;

static std::unordered_map<int, std::shared_ptr<SemaInfo>> g_semas;
static int g_nextSemaId = 1;
static std::mutex g_sema_map_mutex;
static std::unordered_map<int, std::shared_ptr<EventFlagInfo>> g_eventFlags;
static int g_nextEventFlagId = 1;
static std::mutex g_event_flag_map_mutex;
static std::unordered_map<int, std::shared_ptr<AlarmInfo>> g_alarms;
static int g_nextAlarmId = 1;
static std::mutex g_alarm_mutex;
static std::condition_variable g_alarm_cv;
static std::once_flag g_alarm_worker_once;
std::atomic<int> g_activeThreads{0};

struct RpcServerState
{
    uint32_t sid = 0;
    uint32_t sd_ptr = 0; // PS2 address
};

struct RpcClientState
{
    bool busy = false;
    uint32_t last_rpc = 0;
    uint32_t sid = 0;
};

static std::unordered_map<uint32_t, RpcServerState> g_rpc_servers;
static std::unordered_map<uint32_t, RpcClientState> g_rpc_clients;
static std::mutex g_rpc_mutex;
static bool g_rpc_initialized = false;
static uint32_t g_rpc_next_id = 1;
static uint32_t g_rpc_packet_index = 0;
static uint32_t g_rpc_server_index = 0;
static uint32_t g_rpc_active_queue = 0;
static constexpr uint32_t kDtxRpcSid = 0x7D000000u;
static constexpr uint32_t kDtxUrpcObjBase = 0x01F18000u;
static constexpr uint32_t kDtxUrpcObjLimit = 0x01F1FF00u;
static constexpr uint32_t kDtxUrpcFnTableBase = 0x0034FED0u;
static constexpr uint32_t kDtxUrpcObjTableBase = 0x0034FFD0u;
static std::mutex g_dtx_rpc_mutex;
static std::unordered_map<uint32_t, uint32_t> g_dtx_remote_by_id;
static uint32_t g_dtx_next_urpc_obj = kDtxUrpcObjBase;

struct DtxSjrmtState
{
    uint32_t handle = 0;
    uint32_t mode = 0;
    uint32_t wkAddr = 0;
    uint32_t wkSize = 0;
    uint32_t readPos = 0;
    uint32_t writePos = 0;
    uint32_t roomBytes = 0;
    uint32_t dataBytes = 0;
    uint32_t uuid0 = 0;
    uint32_t uuid1 = 0;
    uint32_t uuid2 = 0;
    uint32_t uuid3 = 0;
};

static std::unordered_map<uint32_t, DtxSjrmtState> g_dtx_sjrmt_by_handle;

static uint32_t dtxNormalizeSjrmtCapacity(uint32_t requestedBytes)
{
    if (requestedBytes == 0u || requestedBytes > 0x01000000u)
    {
        return 0x4000u;
    }
    return requestedBytes;
}

static uint32_t dtxAllocUrpcHandleLocked()
{
    for (uint32_t i = 0; i < 4096u; ++i)
    {
        uint32_t candidate = g_dtx_next_urpc_obj;
        g_dtx_next_urpc_obj += 0x20u;
        if (g_dtx_next_urpc_obj < kDtxUrpcObjBase || g_dtx_next_urpc_obj >= kDtxUrpcObjLimit)
        {
            g_dtx_next_urpc_obj = kDtxUrpcObjBase;
        }

        if (candidate < kDtxUrpcObjBase || candidate >= kDtxUrpcObjLimit)
        {
            continue;
        }

        if (g_dtx_sjrmt_by_handle.find(candidate) != g_dtx_sjrmt_by_handle.end())
        {
            continue;
        }

        bool inUseByDtxRemote = false;
        for (const auto &entry : g_dtx_remote_by_id)
        {
            if (entry.second == candidate)
            {
                inUseByDtxRemote = true;
                break;
            }
        }

        if (!inUseByDtxRemote)
        {
            return candidate;
        }
    }

    return kDtxUrpcObjBase;
}

struct ExitHandlerEntry
{
    uint32_t func = 0;
    uint32_t arg = 0;
};

static std::mutex g_exit_handler_mutex;
static std::unordered_map<int, std::vector<ExitHandlerEntry>> g_exit_handlers;

static std::mutex g_bootmode_mutex;
static bool g_bootmode_initialized = false;
static uint32_t g_bootmode_pool_offset = 0;
static std::unordered_map<uint8_t, uint32_t> g_bootmode_addresses;

static std::mutex g_tls_mutex;
static uint32_t g_tls_index = 0;

static std::mutex g_osd_mutex;
static bool g_osd_config_initialized = false;
static uint32_t g_osd_config_raw = 0;

static std::mutex g_ps2_path_mutex;
static bool g_ps2_paths_initialized = false;
static std::filesystem::path g_host_base;
static std::filesystem::path g_cdrom_base;
static std::filesystem::path g_host_cwd;
static std::filesystem::path g_cdrom_cwd;
static std::string g_ps2_cwd_device = "host0";

static constexpr uint32_t kRpcPacketSize = 64;
static constexpr uint32_t kRpcPacketPoolBase = 0x01F00000;
static constexpr uint32_t kRpcPacketPoolBytes = 0x00010000;
static constexpr uint32_t kRpcPacketPoolCount = kRpcPacketPoolBytes / kRpcPacketSize;
static constexpr uint32_t kRpcServerPoolBase = 0x01F10000;
static constexpr uint32_t kRpcServerPoolBytes = 0x00010000;
static constexpr uint32_t kRpcServerStride = 0x80;
static constexpr uint32_t kRpcServerPoolCount = kRpcServerPoolBytes / kRpcServerStride;

static constexpr uint32_t kTlsPoolBase = 0x01F20000;
static constexpr uint32_t kTlsPoolBytes = 0x00010000;
static constexpr uint32_t kTlsBlockSize = 0x100;
static constexpr uint32_t kTlsPoolCount = kTlsPoolBytes / kTlsBlockSize;

static constexpr uint32_t kBootModePoolBase = 0x01F30000;
static constexpr uint32_t kBootModePoolBytes = 0x00001000;

static constexpr uint32_t kSifRpcModeNowait = 0x01;
static constexpr uint32_t kSifRpcModeNoWbDc = 0x02;
static constexpr size_t kMaxSifModulePathBytes = 260;
static constexpr uint32_t kMaxSifModuleLogs = 24;
static constexpr size_t kSifModuleBufferProbeBytes = 2048;
static constexpr size_t kLoadfilePathMaxBytes = 252;
static constexpr size_t kLoadfileArgMaxBytes = 252;
static constexpr uint32_t kElfMagic = 0x464C457Fu;
static constexpr uint16_t kElfMachineMips = 8u;
static constexpr uint16_t kElfTypeExec = 2u;
static constexpr uint32_t kElfPtLoad = 1u;
static constexpr uint32_t kElfPtMipsRegInfo = 0x70000000u;
static constexpr uint32_t kElfShtMipsRegInfo = 0x70000006u;

#pragma pack(push, 1)
struct Elf32Header
{
    uint32_t magic;
    uint8_t elfClass;
    uint8_t endianness;
    uint8_t version;
    uint8_t osAbi;
    uint8_t abiVersion;
    uint8_t pad[7];
    uint16_t type;
    uint16_t machine;
    uint32_t version2;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

struct Elf32ProgramHeader
{
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
};

struct Elf32SectionHeader
{
    uint32_t name;
    uint32_t type;
    uint32_t flags;
    uint32_t addr;
    uint32_t offset;
    uint32_t size;
    uint32_t link;
    uint32_t info;
    uint32_t addralign;
    uint32_t entsize;
};

struct GuestExecData
{
    uint32_t epc;
    uint32_t gp;
    uint32_t sp;
    uint32_t dummy;
};
#pragma pack(pop)

static_assert(sizeof(Elf32Header) == 52u, "Unexpected ELF32 header layout.");
static_assert(sizeof(Elf32ProgramHeader) == 32u, "Unexpected ELF32 program header layout.");
static_assert(sizeof(Elf32SectionHeader) == 40u, "Unexpected ELF32 section header layout.");
static_assert(sizeof(GuestExecData) == 16u, "Unexpected GuestExecData layout.");

struct SifModuleRecord
{
    int32_t id = 0;
    std::string path;
    std::string pathKey;
    uint32_t refCount = 0;
    bool loaded = false;
};

static std::mutex g_sif_module_mutex;
static std::unordered_map<int32_t, SifModuleRecord> g_sif_modules_by_id;
static std::unordered_map<std::string, int32_t> g_sif_module_id_by_path;
static int32_t g_next_sif_module_id = 1;
static uint32_t g_sif_module_log_count = 0;

namespace
{
    std::string readGuestCStringBounded(const uint8_t *rdram, uint32_t guestAddr, size_t maxBytes)
    {
        std::string out;
        if (!rdram || guestAddr == 0 || maxBytes == 0)
        {
            return out;
        }

        out.reserve(maxBytes);
        for (size_t i = 0; i < maxBytes; ++i)
        {
            const char ch = static_cast<char>(rdram[(guestAddr + static_cast<uint32_t>(i)) & PS2_RAM_MASK]);
            if (ch == '\0')
            {
                break;
            }
            out.push_back(ch);
        }
        return out;
    }

    std::string normalizeSifModulePathKey(const std::string &path)
    {
        return toLowerAscii(normalizePs2PathSuffix(path));
    }

    uint64_t hashGuestBytesFnv1a64(const uint8_t *rdram, uint32_t guestAddr, size_t byteCount)
    {
        constexpr uint64_t kOffset = 1469598103934665603ull;
        constexpr uint64_t kPrime = 1099511628211ull;

        if (!rdram || guestAddr == 0 || byteCount == 0)
        {
            return 0ull;
        }

        uint64_t hash = kOffset;
        for (size_t i = 0; i < byteCount; ++i)
        {
            const uint8_t b = rdram[(guestAddr + static_cast<uint32_t>(i)) & PS2_RAM_MASK];
            hash ^= static_cast<uint64_t>(b);
            hash *= kPrime;
        }
        return hash;
    }

    std::string makeSifModuleBufferTag(const uint8_t *rdram, uint32_t bufferAddr)
    {
        char key[96] = {};
        const uint64_t hash = hashGuestBytesFnv1a64(rdram, bufferAddr, kSifModuleBufferProbeBytes);
        std::snprintf(key, sizeof(key), "iopbuf:fnv64:%016llx", static_cast<unsigned long long>(hash));
        return std::string(key);
    }

    void logSifModuleAction(const char *op, int32_t moduleId, const std::string &path, uint32_t refCount)
    {
        if (!op)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(g_sif_module_mutex);
        if (g_sif_module_log_count >= kMaxSifModuleLogs)
        {
            return;
        }

        std::cout << "[SIF module] " << op
                  << " id=" << moduleId
                  << " ref=" << refCount
                  << " path=\"" << path << "\""
                  << std::endl;
        ++g_sif_module_log_count;
    }

    int32_t trackSifModuleLoad(const std::string &path)
    {
        if (path.empty())
        {
            return -1;
        }

        const std::string pathKey = normalizeSifModulePathKey(path);
        if (pathKey.empty())
        {
            return -1;
        }

        std::lock_guard<std::mutex> lock(g_sif_module_mutex);

        auto byPathIt = g_sif_module_id_by_path.find(pathKey);
        if (byPathIt != g_sif_module_id_by_path.end())
        {
            auto byIdIt = g_sif_modules_by_id.find(byPathIt->second);
            if (byIdIt != g_sif_modules_by_id.end())
            {
                SifModuleRecord &record = byIdIt->second;
                record.loaded = true;
                ++record.refCount;
                return record.id;
            }
        }

        if (g_next_sif_module_id <= 0)
        {
            g_next_sif_module_id = 1;
        }

        const int32_t moduleId = g_next_sif_module_id++;
        SifModuleRecord record;
        record.id = moduleId;
        record.path = path;
        record.pathKey = pathKey;
        record.refCount = 1;
        record.loaded = true;

        g_sif_module_id_by_path[pathKey] = moduleId;
        g_sif_modules_by_id[moduleId] = record;
        return moduleId;
    }

    bool trackSifModuleStop(int32_t moduleId, uint32_t *remainingRefs = nullptr)
    {
        if (moduleId <= 0)
        {
            if (remainingRefs)
            {
                *remainingRefs = 0;
            }
            return false;
        }

        std::lock_guard<std::mutex> lock(g_sif_module_mutex);
        auto it = g_sif_modules_by_id.find(moduleId);
        if (it == g_sif_modules_by_id.end())
        {
            if (remainingRefs)
            {
                *remainingRefs = 0;
            }
            return false;
        }

        SifModuleRecord &record = it->second;
        if (record.refCount > 0)
        {
            --record.refCount;
        }
        record.loaded = (record.refCount != 0);

        if (remainingRefs)
        {
            *remainingRefs = record.refCount;
        }
        return true;
    }

    bool readFileBlockAt(std::ifstream &file, uint64_t offset, void *dst, size_t byteCount)
    {
        if (!dst || byteCount == 0)
        {
            return false;
        }

        file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!file)
        {
            return false;
        }

        file.read(reinterpret_cast<char *>(dst), static_cast<std::streamsize>(byteCount));
        return file.gcount() == static_cast<std::streamsize>(byteCount);
    }

    bool tryExtractElfGpValue(std::ifstream &file, const Elf32Header &header, uint32_t &gpOut)
    {
        uint8_t regInfo[24] = {};

        for (uint32_t i = 0; i < header.phnum; ++i)
        {
            Elf32ProgramHeader ph{};
            const uint64_t phOffset = static_cast<uint64_t>(header.phoff) + static_cast<uint64_t>(i) * header.phentsize;
            if (!readFileBlockAt(file, phOffset, &ph, sizeof(ph)))
            {
                return false;
            }

            if (ph.type == kElfPtMipsRegInfo && ph.filesz >= sizeof(regInfo))
            {
                if (!readFileBlockAt(file, ph.offset, regInfo, sizeof(regInfo)))
                {
                    return false;
                }
                std::memcpy(&gpOut, regInfo + 20u, sizeof(gpOut));
                return true;
            }
        }

        for (uint32_t i = 0; i < header.shnum; ++i)
        {
            Elf32SectionHeader sh{};
            const uint64_t shOffset = static_cast<uint64_t>(header.shoff) + static_cast<uint64_t>(i) * header.shentsize;
            if (!readFileBlockAt(file, shOffset, &sh, sizeof(sh)))
            {
                return false;
            }

            if (sh.type == kElfShtMipsRegInfo && sh.size >= sizeof(regInfo))
            {
                if (!readFileBlockAt(file, sh.offset, regInfo, sizeof(regInfo)))
                {
                    return false;
                }
                std::memcpy(&gpOut, regInfo + 20u, sizeof(gpOut));
                return true;
            }
        }

        return false;
    }

    bool loadElfIntoGuestMemory(const std::string &hostPath,
                                uint8_t *rdram,
                                PS2Runtime *runtime,
                                const std::string &sectionName,
                                GuestExecData &execDataOut,
                                std::string &errorOut)
    {
        if (!rdram || hostPath.empty())
        {
            errorOut = "invalid path or RDRAM pointer";
            return false;
        }

        std::ifstream file(hostPath, std::ios::binary);
        if (!file)
        {
            errorOut = "failed to open ELF";
            return false;
        }

        Elf32Header header{};
        if (!readFileBlockAt(file, 0, &header, sizeof(header)))
        {
            errorOut = "failed to read ELF header";
            return false;
        }

        if (header.magic != kElfMagic || header.machine != kElfMachineMips || header.type != kElfTypeExec)
        {
            errorOut = "not a MIPS executable ELF";
            return false;
        }

        bool loadedAny = false;
        const bool loadAll = sectionName.empty() || toLowerAscii(sectionName) == "all";
        static uint32_t secFilterLogCount = 0;
        if (!loadAll && secFilterLogCount < 8u)
        {
            std::cout << "[SifLoadElfPart] section filter \"" << sectionName
                      << "\" requested; loading PT_LOAD segments only." << std::endl;
            ++secFilterLogCount;
        }

        for (uint32_t i = 0; i < header.phnum; ++i)
        {
            Elf32ProgramHeader ph{};
            const uint64_t phOffset = static_cast<uint64_t>(header.phoff) + static_cast<uint64_t>(i) * header.phentsize;
            if (!readFileBlockAt(file, phOffset, &ph, sizeof(ph)))
            {
                errorOut = "failed to read ELF program headers";
                return false;
            }

            if (ph.type != kElfPtLoad || ph.memsz == 0u)
            {
                continue;
            }
            if (ph.filesz > ph.memsz)
            {
                errorOut = "ELF segment filesz > memsz";
                return false;
            }

            const uint64_t memSize64 = static_cast<uint64_t>(ph.memsz);
            if (runtime && ph.vaddr >= PS2_SCRATCHPAD_BASE && ph.vaddr < (PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE))
            {
                const uint32_t scratchOffset = runtime->memory().translateAddress(ph.vaddr);
                if (static_cast<uint64_t>(scratchOffset) + memSize64 > PS2_SCRATCHPAD_SIZE)
                {
                    errorOut = "ELF scratchpad segment out of range";
                    return false;
                }

                uint8_t *dest = runtime->memory().getScratchpad() + scratchOffset;
                if (ph.filesz > 0u)
                {
                    if (!readFileBlockAt(file, ph.offset, dest, ph.filesz))
                    {
                        errorOut = "failed to read ELF segment payload";
                        return false;
                    }
                }
                if (ph.memsz > ph.filesz)
                {
                    std::memset(dest + ph.filesz, 0, ph.memsz - ph.filesz);
                }
            }
            else
            {
                const uint32_t physAddr = runtime ? runtime->memory().translateAddress(ph.vaddr) : (ph.vaddr & PS2_RAM_MASK);
                if (static_cast<uint64_t>(physAddr) + memSize64 > PS2_RAM_SIZE)
                {
                    errorOut = "ELF RDRAM segment out of range";
                    return false;
                }

                uint8_t *dest = rdram + physAddr;
                if (ph.filesz > 0u)
                {
                    if (!readFileBlockAt(file, ph.offset, dest, ph.filesz))
                    {
                        errorOut = "failed to read ELF segment payload";
                        return false;
                    }
                }
                if (ph.memsz > ph.filesz)
                {
                    std::memset(dest + ph.filesz, 0, ph.memsz - ph.filesz);
                }
            }

            loadedAny = true;
        }

        if (!loadedAny)
        {
            errorOut = "ELF has no loadable segments";
            return false;
        }

        execDataOut.epc = header.entry;
        execDataOut.gp = 0u;
        execDataOut.sp = 0u;
        execDataOut.dummy = 0u;

        uint32_t gpValue = 0u;
        if (tryExtractElfGpValue(file, header, gpValue))
        {
            execDataOut.gp = gpValue;
        }

        return true;
    }

    int32_t runSifLoadElfPart(uint8_t *rdram,
                              R5900Context *ctx,
                              PS2Runtime *runtime,
                              uint32_t pathAddr,
                              const std::string &sectionName,
                              uint32_t execDataAddr)
    {
        if (!rdram || !ctx)
        {
            return -1;
        }

        const std::string ps2Path = readGuestCStringBounded(rdram, pathAddr, kLoadfilePathMaxBytes);
        if (ps2Path.empty())
        {
            return -1;
        }

        const std::string hostPath = translatePs2Path(ps2Path.c_str());
        if (hostPath.empty())
        {
            return -1;
        }

        GuestExecData execData{};
        std::string loadError;
        if (!loadElfIntoGuestMemory(hostPath, rdram, runtime, sectionName, execData, loadError))
        {
            static uint32_t logCount = 0;
            if (logCount < 16u)
            {
                std::cerr << "[SifLoadElfPart] failed path=\"" << ps2Path << "\" host=\"" << hostPath
                          << "\" reason=" << loadError << std::endl;
                ++logCount;
            }
            return -1;
        }

        if (execData.gp == 0u)
        {
            execData.gp = getRegU32(ctx, 28);
        }
        execData.sp = getRegU32(ctx, 29);

        if (execDataAddr != 0u)
        {
            GuestExecData *guestExec = reinterpret_cast<GuestExecData *>(getMemPtr(rdram, execDataAddr));
            if (!guestExec)
            {
                return -1;
            }
            std::memcpy(guestExec, &execData, sizeof(execData));
        }

        static uint32_t successLogs = 0;
        if (successLogs < 16u)
        {
            std::cout << "[SifLoadElfPart] loaded \"" << ps2Path << "\" epc=0x"
                      << std::hex << execData.epc << " gp=0x" << execData.gp << std::dec << std::endl;
            ++successLogs;
        }

        return 0;
    }
}

namespace
{
    struct ThreadExitException final : public std::exception
    {
        const char *what() const noexcept override
        {
            return "PS2 Thread Exit";
        }
    };
}

static void applySuspendStatusLocked(ThreadInfo &info)
{
    if (info.waitType != TSW_NONE)
    {
        info.status = THS_WAITSUSPEND;
    }
    else
    {
        info.status = THS_SUSPEND;
    }
}

static void throwIfTerminated(const std::shared_ptr<ThreadInfo> &info)
{
    if (info && info->terminated.load())
    {
        throw ThreadExitException();
    }
}

static void waitWhileSuspended(const std::shared_ptr<ThreadInfo> &info)
{
    if (!info)
        return;

    std::unique_lock<std::mutex> lock(info->m);
    if (info->suspendCount > 0)
    {
        info->status = THS_SUSPEND;
        info->waitType = TSW_NONE;
        info->waitId = 0;
        info->cv.wait(lock, [&]()
                      { return info->suspendCount == 0 || info->terminated.load(); });
        if (info->terminated.load())
        {
            throw ThreadExitException();
        }
        info->status = THS_RUN;
    }
}

static std::shared_ptr<ThreadInfo> lookupThreadInfo(int tid)
{
    std::lock_guard<std::mutex> lock(g_thread_map_mutex);
    auto it = g_threads.find(tid);
    if (it != g_threads.end())
    {
        return it->second;
    }
    return nullptr;
}

static std::shared_ptr<ThreadInfo> ensureCurrentThreadInfo(R5900Context *ctx)
{
    const int tid = g_currentThreadId;
    std::lock_guard<std::mutex> lock(g_thread_map_mutex);
    auto it = g_threads.find(tid);
    if (it != g_threads.end())
    {
        return it->second;
    }

    auto info = std::make_shared<ThreadInfo>();
    info->started = true;
    info->status = THS_RUN;
    info->currentPriority = info->priority;
    info->suspendCount = 0;
    if (ctx)
    {
        info->entry = ctx->pc;
        info->stack = getRegU32(ctx, 29);
        info->gp = getRegU32(ctx, 28);
    }
    info->waitType = TSW_NONE;
    info->waitId = 0;

    g_threads.emplace(tid, info);
    return info;
}

static std::shared_ptr<SemaInfo> lookupSemaInfo(int sid)
{
    std::lock_guard<std::mutex> lock(g_sema_map_mutex);
    auto it = g_semas.find(sid);
    if (it != g_semas.end())
    {
        return it->second;
    }
    return nullptr;
}

static std::shared_ptr<EventFlagInfo> lookupEventFlagInfo(int eid)
{
    std::lock_guard<std::mutex> lock(g_event_flag_map_mutex);
    auto it = g_eventFlags.find(eid);
    if (it != g_eventFlags.end())
    {
        return it->second;
    }
    return nullptr;
}

static void setRegU32(R5900Context *ctx, int reg, uint32_t value)
{
    if (reg < 0 || reg > 31)
        return;
    ctx->r[reg] = _mm_set_epi32(0, 0, 0, value);
}

static std::chrono::microseconds alarmTicksToDuration(uint16_t ticks)
{
    constexpr uint64_t kAlarmTickUsec = 64u; // Approximate EE H-SYNC tick period.
    const uint64_t clampedTicks = (ticks == 0u) ? 1u : static_cast<uint64_t>(ticks);
    return std::chrono::microseconds(clampedTicks * kAlarmTickUsec);
}

static void ensureAlarmWorkerRunning()
{
    std::call_once(g_alarm_worker_once, []()
                   {
        std::thread([]()
                    {
            for (;;)
            {
                std::shared_ptr<AlarmInfo> readyAlarm;
                {
                    std::unique_lock<std::mutex> lock(g_alarm_mutex);
                    while (!readyAlarm)
                    {
                        if (g_alarms.empty())
                        {
                            g_alarm_cv.wait(lock);
                            continue;
                        }

                        auto nextIt = std::min_element(g_alarms.begin(), g_alarms.end(),
                                                       [](const auto &a, const auto &b)
                                                       {
                                                           return a.second->dueAt < b.second->dueAt;
                                                       });
                        if (nextIt == g_alarms.end())
                        {
                            g_alarm_cv.wait(lock);
                            continue;
                        }

                        const auto now = std::chrono::steady_clock::now();
                        if (nextIt->second->dueAt > now)
                        {
                            g_alarm_cv.wait_until(lock, nextIt->second->dueAt);
                            continue;
                        }

                        readyAlarm = nextIt->second;
                        g_alarms.erase(nextIt);
                    }
                }

                if (!readyAlarm || !readyAlarm->runtime || !readyAlarm->rdram || !readyAlarm->handler)
                {
                    continue;
                }
                if (!readyAlarm->runtime->hasFunction(readyAlarm->handler))
                {
                    continue;
                }

                try
                {
                    R5900Context callbackCtx{};
                    setRegU32(&callbackCtx, 28, readyAlarm->gp);
                    setRegU32(&callbackCtx, 29, readyAlarm->sp);
                    setRegU32(&callbackCtx, 31, 0);
                    setRegU32(&callbackCtx, 4, static_cast<uint32_t>(readyAlarm->id));
                    setRegU32(&callbackCtx, 5, static_cast<uint32_t>(readyAlarm->ticks));
                    setRegU32(&callbackCtx, 6, readyAlarm->commonArg);
                    setRegU32(&callbackCtx, 7, 0);
                    callbackCtx.pc = readyAlarm->handler;

                    PS2Runtime::RecompiledFunction func = readyAlarm->runtime->lookupFunction(readyAlarm->handler);
                    func(readyAlarm->rdram, &callbackCtx, readyAlarm->runtime);
                }
                catch (const ThreadExitException &)
                {
                }
                catch (const std::exception &e)
                {
                    static int alarmExceptionLogs = 0;
                    if (alarmExceptionLogs < 8)
                    {
                        std::cerr << "[SetAlarm] callback exception: " << e.what() << std::endl;
                        ++alarmExceptionLogs;
                    }
                }
            } })
            .detach(); });
}

static void rpcCopyToRdram(uint8_t *rdram, uint32_t dst, uint32_t src, size_t size)
{
    if (!rdram || size == 0)
        return;

    constexpr size_t kMaxRpcTransferBytes = 1u * 1024u * 1024u;
    const size_t clampedSize = std::min(size, kMaxRpcTransferBytes);
    if (clampedSize != size)
    {
        static uint32_t warnCount = 0;
        if (warnCount < 8)
        {
            std::cerr << "[SifCallRpc] clamping copy size from " << size
                      << " to " << clampedSize
                      << " bytes (dst=0x" << std::hex << dst
                      << " src=0x" << src << std::dec << ")" << std::endl;
            ++warnCount;
        }
    }

    for (size_t i = 0; i < clampedSize; ++i)
    {
        const uint32_t dstAddr = dst + static_cast<uint32_t>(i);
        const uint32_t srcAddr = src + static_cast<uint32_t>(i);
        uint8_t *dstPtr = getMemPtr(rdram, dstAddr);
        const uint8_t *srcPtr = getConstMemPtr(rdram, srcAddr);
        if (!dstPtr || !srcPtr)
        {
            break;
        }
        *dstPtr = *srcPtr;
    }
}

static void rpcZeroRdram(uint8_t *rdram, uint32_t dst, size_t size)
{
    if (!rdram || size == 0)
        return;

    constexpr size_t kMaxRpcTransferBytes = 1u * 1024u * 1024u;
    const size_t clampedSize = std::min(size, kMaxRpcTransferBytes);
    if (clampedSize != size)
    {
        static uint32_t warnCount = 0;
        if (warnCount < 8)
        {
            std::cerr << "[SifCallRpc] clamping zero size from " << size
                      << " to " << clampedSize
                      << " bytes (dst=0x" << std::hex << dst << std::dec << ")" << std::endl;
            ++warnCount;
        }
    }

    for (size_t i = 0; i < clampedSize; ++i)
    {
        const uint32_t dstAddr = dst + static_cast<uint32_t>(i);
        uint8_t *dstPtr = getMemPtr(rdram, dstAddr);
        if (!dstPtr)
        {
            break;
        }
        *dstPtr = 0;
    }
}

static bool readStackU32(uint8_t *rdram, uint32_t sp, uint32_t offset, uint32_t &out)
{
    uint8_t *ptr = getMemPtr(rdram, sp + offset);
    if (!ptr)
        return false;
    out = *reinterpret_cast<uint32_t *>(ptr);
    return true;
}

static bool rpcInvokeFunction(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime,
                              uint32_t funcAddr, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t *outV0)
{
    if (!runtime || !funcAddr || !runtime->hasFunction(funcAddr))
        return false;

    R5900Context tmp = *ctx;
    setRegU32(&tmp, 4, a0);
    setRegU32(&tmp, 5, a1);
    setRegU32(&tmp, 6, a2);
    setRegU32(&tmp, 7, a3);
    tmp.pc = funcAddr;

    PS2Runtime::RecompiledFunction func = runtime->lookupFunction(funcAddr);
    func(rdram, &tmp, runtime);

    if (outV0)
    {
        *outV0 = getRegU32(&tmp, 2);
    }
    return true;
}

static uint32_t rpcAllocPacketAddr(uint8_t *rdram)
{
    if (kRpcPacketPoolCount == 0)
        return 0;

    uint32_t slot = g_rpc_packet_index++ % kRpcPacketPoolCount;
    uint32_t addr = kRpcPacketPoolBase + (slot * kRpcPacketSize);
    rpcZeroRdram(rdram, addr, kRpcPacketSize);
    return addr;
}

static uint32_t rpcAllocServerAddr(uint8_t *rdram)
{
    if (kRpcServerPoolCount == 0)
        return 0;

    uint32_t slot = g_rpc_server_index++ % kRpcServerPoolCount;
    uint32_t addr = kRpcServerPoolBase + (slot * kRpcServerStride);
    rpcZeroRdram(rdram, addr, kRpcServerStride);
    return addr;
}
/*
struct SemaInfo
{
    int count = 0;
    int maxCount = 0;
    std::mutex m;
    std::condition_variable cv;
};

static std::unordered_map<int, ThreadInfo> g_threads;
static int g_nextThreadId = 2; // Reserve 1 for the main thread
static thread_local int g_currentThreadId = 1;

static std::unordered_map<int, std::shared_ptr<SemaInfo>> g_semas;
static int g_nextSemaId = 1;
std::atomic<int> g_activeThreads{0};
*/

struct IrqHandlerInfo
{
    uint32_t cause = 0;
    uint32_t handler = 0;
    uint32_t arg = 0;
    bool enabled = true;
};

static std::unordered_map<int, IrqHandlerInfo> g_intcHandlers;
static std::unordered_map<int, IrqHandlerInfo> g_dmacHandlers;
static int g_nextIntcHandlerId = 1;
static int g_nextDmacHandlerId = 1;

int allocatePs2Fd(FILE *file)
{
    if (!file)
        return -1;
    int fd = g_nextFd++;
    g_fileDescriptors[fd] = file;
    return fd;
}

FILE *getHostFile(int ps2Fd)
{
    auto it = g_fileDescriptors.find(ps2Fd);
    if (it != g_fileDescriptors.end())
    {
        return it->second;
    }
    return nullptr;
}

void releasePs2Fd(int ps2Fd)
{
    g_fileDescriptors.erase(ps2Fd);
}

const char *translateFioMode(int ps2Flags)
{
    bool read = (ps2Flags & PS2_FIO_O_RDONLY) || (ps2Flags & PS2_FIO_O_RDWR);
    bool write = (ps2Flags & PS2_FIO_O_WRONLY) || (ps2Flags & PS2_FIO_O_RDWR);
    bool append = (ps2Flags & PS2_FIO_O_APPEND);
    bool create = (ps2Flags & PS2_FIO_O_CREAT);
    bool truncate = (ps2Flags & PS2_FIO_O_TRUNC);

    if (read && write)
    {
        if (create && truncate)
            return "w+b";
        if (create)
            return "a+b";
        return "r+b";
    }
    else if (write)
    {
        if (append)
            return "ab";
        if (create && truncate)
            return "wb";
        if (create)
            return "wx";
        return "r+b";
    }
    else if (read)
    {
        return "rb";
    }
    return "rb";
}

std::string translatePs2Path(const char *ps2Path)
{
    if (!ps2Path || !*ps2Path)
    {
        return {};
    }

    std::string pathStr(ps2Path);
    std::string lower = toLowerAscii(pathStr);

    auto resolveWithBase = [&](const std::filesystem::path &base, const std::string &suffix) -> std::string
    {
        const std::string normalizedSuffix = normalizePs2PathSuffix(suffix);
        std::filesystem::path resolved = base;
        if (!normalizedSuffix.empty())
        {
            resolved /= std::filesystem::path(normalizedSuffix);
        }
        return resolved.lexically_normal().string();
    };

    if (lower.rfind("host0:", 0) == 0 || lower.rfind("host:", 0) == 0)
    {
        const std::size_t prefixLength = (lower.rfind("host0:", 0) == 0) ? 6 : 5;
        return resolveWithBase(getConfiguredHostRoot(), pathStr.substr(prefixLength));
    }

    if (lower.rfind("cdrom0:", 0) == 0 || lower.rfind("cdrom:", 0) == 0)
    {
        const std::size_t prefixLength = (lower.rfind("cdrom0:", 0) == 0) ? 7 : 6;
        return resolveWithBase(getConfiguredCdRoot(), pathStr.substr(prefixLength));
    }

    if (!pathStr.empty() && (pathStr.front() == '/' || pathStr.front() == '\\'))
    {
        return resolveWithBase(getConfiguredCdRoot(), pathStr);
    }

    if (pathStr.size() > 1 && pathStr[1] == ':')
    {
        return pathStr;
    }

    return resolveWithBase(getConfiguredCdRoot(), pathStr);
}

static bool localtimeSafe(const std::time_t *t, std::tm *out)
{
#ifdef _WIN32
    return localtime_s(out, t) == 0;
#else
    return localtime_r(t, out) != nullptr;
#endif
}

static void encodePs2Time(std::time_t t, uint8_t out[8])
{
    std::tm tm{};
    if (!localtimeSafe(&t, &tm))
    {
        std::memset(out, 0, 8);
        return;
    }

    uint16_t year = static_cast<uint16_t>(tm.tm_year + 1900);
    out[0] = 0;
    out[1] = static_cast<uint8_t>(tm.tm_sec);
    out[2] = static_cast<uint8_t>(tm.tm_min);
    out[3] = static_cast<uint8_t>(tm.tm_hour);
    out[4] = static_cast<uint8_t>(tm.tm_mday);
    out[5] = static_cast<uint8_t>(tm.tm_mon + 1);
    out[6] = static_cast<uint8_t>(year & 0xFF);
    out[7] = static_cast<uint8_t>((year >> 8) & 0xFF);
}

static std::time_t fileTimeToTimeT(std::filesystem::file_time_type ft)
{
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(sctp);
}

static bool gmtimeSafe(const std::time_t *t, std::tm *out)
{
#ifdef _WIN32
    return gmtime_s(out, t) == 0;
#else
    return gmtime_r(t, out) != nullptr;
#endif
}

static int getTimezoneOffsetMinutes()
{
    std::time_t now = std::time(nullptr);
    std::tm local{};
    std::tm gmt{};
    if (!localtimeSafe(&now, &local) || !gmtimeSafe(&now, &gmt))
        return 0;

    std::time_t localTime = std::mktime(&local);
    std::time_t gmtTime = std::mktime(&gmt);
    if (localTime == static_cast<std::time_t>(-1) || gmtTime == static_cast<std::time_t>(-1))
        return 0;

    double diff = std::difftime(localTime, gmtTime);
    return static_cast<int>(diff / 60.0);
}

static uint32_t packOsdConfig(uint32_t spdifMode, uint32_t screenType, uint32_t videoOutput,
                              uint32_t japLanguage, uint32_t ps1drvConfig, uint32_t version,
                              uint32_t language, int timezoneOffset)
{
    uint32_t raw = 0;
    raw |= (spdifMode & 0x1) << 0;
    raw |= (screenType & 0x3) << 1;
    raw |= (videoOutput & 0x1) << 3;
    raw |= (japLanguage & 0x1) << 4;
    raw |= (ps1drvConfig & 0xFF) << 5;
    raw |= (version & 0x7) << 13;
    raw |= (language & 0x1F) << 16;
    raw |= (static_cast<uint32_t>(timezoneOffset) & 0x7FF) << 21;
    return raw;
}

static int decodeTimezoneOffset(uint32_t raw)
{
    int tz = static_cast<int>((raw >> 21) & 0x7FF);
    if (tz & 0x400)
        tz |= ~0x7FF;
    return tz;
}

static int clampTimezoneOffset(int tz)
{
    if (tz < -1024)
        return -1024;
    if (tz > 1023)
        return 1023;
    return tz;
}

static uint32_t sanitizeOsdConfigRaw(uint32_t raw)
{
    uint32_t spdifMode = raw & 0x1;
    uint32_t screenType = (raw >> 1) & 0x3;
    if (screenType > 2)
        screenType = 0;
    uint32_t videoOutput = (raw >> 3) & 0x1;
    uint32_t japLanguage = (raw >> 4) & 0x1;
    uint32_t ps1drvConfig = (raw >> 5) & 0xFF;
    uint32_t version = (raw >> 13) & 0x7;
    if (version > 2)
        version = 1;
    uint32_t language = (raw >> 16) & 0x1F;
    int tz = clampTimezoneOffset(decodeTimezoneOffset(raw));
    return packOsdConfig(spdifMode, screenType, videoOutput, japLanguage, ps1drvConfig, version, language, tz);
}

static void ensureOsdConfigInitialized()
{
    std::lock_guard<std::mutex> lock(g_osd_mutex);
    if (g_osd_config_initialized)
        return;

    int tz = clampTimezoneOffset(getTimezoneOffsetMinutes());
    uint32_t spdifMode = 1;   // disabled
    uint32_t screenType = 0;  // 4:3
    uint32_t videoOutput = 0; // RGB
    uint32_t japLanguage = 1; // non-japanese
    uint32_t ps1drvConfig = 0;
    uint32_t version = 1;  // OSD2
    uint32_t language = 1; // English
    g_osd_config_raw = packOsdConfig(spdifMode, screenType, videoOutput, japLanguage, ps1drvConfig, version, language, tz);
    g_osd_config_initialized = true;
}

static uint32_t allocTlsAddr(uint8_t *rdram)
{
    if (!rdram || kTlsPoolCount == 0)
        return 0;

    std::lock_guard<std::mutex> lock(g_tls_mutex);
    uint32_t slot = g_tls_index++ % kTlsPoolCount;
    uint32_t addr = kTlsPoolBase + (slot * kTlsBlockSize);
    rpcZeroRdram(rdram, addr, kTlsBlockSize);
    return addr;
}

static uint32_t allocBootModeAddr(uint8_t *rdram, size_t bytes)
{
    if (!rdram)
        return 0;

    size_t aligned = (bytes + 15u) & ~15u;
    if (g_bootmode_pool_offset + aligned > kBootModePoolBytes)
        return 0;

    uint32_t addr = kBootModePoolBase + g_bootmode_pool_offset;
    g_bootmode_pool_offset += static_cast<uint32_t>(aligned);
    rpcZeroRdram(rdram, addr, aligned);
    return addr;
}

static uint32_t createBootModeEntry(uint8_t *rdram, uint8_t id, uint16_t value, uint8_t lenField, const uint32_t *data, uint8_t dataCount)
{
    uint8_t allocCount = (dataCount == 0) ? 1 : dataCount;
    size_t bytes = static_cast<size_t>(1 + allocCount) * sizeof(uint32_t);
    uint32_t addr = allocBootModeAddr(rdram, bytes);
    if (!addr)
        return 0;

    uint32_t header = (static_cast<uint32_t>(lenField) << 24) |
                      (static_cast<uint32_t>(id) << 16) |
                      (static_cast<uint32_t>(value) & 0xFFFFu);

    uint32_t *dst = reinterpret_cast<uint32_t *>(getMemPtr(rdram, addr));
    if (!dst)
        return 0;

    dst[0] = header;
    for (uint8_t i = 0; i < allocCount; ++i)
    {
        dst[1 + i] = (data && i < dataCount) ? data[i] : 0;
    }

    return addr;
}

static void ensureBootModeTable(uint8_t *rdram)
{
    std::lock_guard<std::mutex> lock(g_bootmode_mutex);
    if (g_bootmode_initialized)
        return;

    g_bootmode_pool_offset = 0;
    g_bootmode_addresses.clear();

    const uint32_t boot3Data[1] = {0};
    const uint32_t boot5Data[1] = {0};

    g_bootmode_addresses[1] = createBootModeEntry(rdram, 1, 0, 0, nullptr, 0);
    g_bootmode_addresses[3] = createBootModeEntry(rdram, 3, 0, 1, boot3Data, 1);
    g_bootmode_addresses[4] = createBootModeEntry(rdram, 4, 0, 0, nullptr, 0);
    g_bootmode_addresses[5] = createBootModeEntry(rdram, 5, 0, 1, boot5Data, 1);
    g_bootmode_addresses[6] = createBootModeEntry(rdram, 6, 0, 0, nullptr, 0);
    g_bootmode_addresses[7] = createBootModeEntry(rdram, 7, 0, 0, nullptr, 0);

    g_bootmode_initialized = true;
}

static void runExitHandlersForThread(int tid, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (!runtime || !ctx)
        return;

    std::vector<ExitHandlerEntry> handlers;
    {
        std::lock_guard<std::mutex> lock(g_exit_handler_mutex);
        auto it = g_exit_handlers.find(tid);
        if (it == g_exit_handlers.end())
            return;
        handlers = std::move(it->second);
        g_exit_handlers.erase(it);
    }

    for (const auto &handler : handlers)
    {
        if (!handler.func)
            continue;
        try
        {
            rpcInvokeFunction(rdram, ctx, runtime, handler.func, handler.arg, 0, 0, 0, nullptr);
        }
        catch (const ThreadExitException &)
        {
            // ignore
        }
        catch (const std::exception &)
        {
        }
    }
}

namespace ps2_syscalls
{
    // for some bizarre case I have to duplicate this here
    void AddIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void RemoveIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void AddDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void RemoveDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void EnableIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void DisableIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void EnableDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void DisableDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SetupHeap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void EndOfHeap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    bool dispatchNumericSyscall(uint32_t syscallNumber, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        switch (syscallNumber)
        {
        case 0x01:
            ResetEE(rdram, ctx, runtime);
            return true;
        case 0x02:
            GsSetCrt(rdram, ctx, runtime);
            return true;
        case 0x04:
            ExitThread(rdram, ctx, runtime);
            return true;
        case 0x10:
            AddIntcHandler(rdram, ctx, runtime);
            return true;
        case 0x11:
            RemoveIntcHandler(rdram, ctx, runtime);
            return true;
        case 0x12:
            AddDmacHandler(rdram, ctx, runtime);
            return true;
        case 0x13:
            RemoveDmacHandler(rdram, ctx, runtime);
            return true;
        case 0x14:
            EnableIntc(rdram, ctx, runtime);
            return true;
        case 0x15:
            DisableIntc(rdram, ctx, runtime);
            return true;
        case 0x16:
            EnableDmac(rdram, ctx, runtime);
            return true;
        case 0x17:
            DisableDmac(rdram, ctx, runtime);
            return true;
        case 0x18:
        case 0xFC:
            SetAlarm(rdram, ctx, runtime);
            return true;
        case 0x19:
        case 0xFE:
            CancelAlarm(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x1E):
        case static_cast<uint32_t>(-0xFD):
            iSetAlarm(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x1F):
        case static_cast<uint32_t>(-0xFF):
            iCancelAlarm(rdram, ctx, runtime);
            return true;
        case 0x20:
            CreateThread(rdram, ctx, runtime);
            return true;
        case 0x21:
            DeleteThread(rdram, ctx, runtime);
            return true;
        case 0x22:
            StartThread(rdram, ctx, runtime);
            return true;
        case 0x23:
            ExitThread(rdram, ctx, runtime);
            return true;
        case 0x24:
            ExitDeleteThread(rdram, ctx, runtime);
            return true;
        case 0x25:
            TerminateThread(rdram, ctx, runtime);
            return true;
        case 0x29:
        case static_cast<uint32_t>(-0x2A):
            ChangeThreadPriority(rdram, ctx, runtime);
            return true;
        case 0x2B:
        case static_cast<uint32_t>(-0x2C):
            RotateThreadReadyQueue(rdram, ctx, runtime);
            return true;
        case 0x2D:
            ReleaseWaitThread(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x2E):
            iReleaseWaitThread(rdram, ctx, runtime);
            return true;
        case 0x2F:
        case static_cast<uint32_t>(-0x2F):
            GetThreadId(rdram, ctx, runtime);
            return true;
        case 0x30:
        case static_cast<uint32_t>(-0x31):
            ReferThreadStatus(rdram, ctx, runtime);
            return true;
        case 0x32:
            SleepThread(rdram, ctx, runtime);
            return true;
        case 0x33:
            WakeupThread(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x34):
            iWakeupThread(rdram, ctx, runtime);
            return true;
        case 0x35:
            CancelWakeupThread(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x36):
            iCancelWakeupThread(rdram, ctx, runtime);
            return true;
        case 0x37:
        case static_cast<uint32_t>(-0x38):
            SuspendThread(rdram, ctx, runtime);
            return true;
        case 0x39:
        case static_cast<uint32_t>(-0x3A):
            ResumeThread(rdram, ctx, runtime);
            return true;
        case 0x3C:
            SetupThread(rdram, ctx, runtime);
            return true;
        case 0x3D:
            SetupHeap(rdram, ctx, runtime);
            return true;
        case 0x3E:
            EndOfHeap(rdram, ctx, runtime);
            return true;
        case 0x40:
            CreateSema(rdram, ctx, runtime);
            return true;
        case 0x41:
        case static_cast<uint32_t>(-0x49):
            DeleteSema(rdram, ctx, runtime);
            return true;
        case 0x42:
            SignalSema(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x43):
            iSignalSema(rdram, ctx, runtime);
            return true;
        case 0x44:
            WaitSema(rdram, ctx, runtime);
            return true;
        case 0x45:
            PollSema(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x46):
            iPollSema(rdram, ctx, runtime);
            return true;
        case 0x47:
            ReferSemaStatus(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x48):
            iReferSemaStatus(rdram, ctx, runtime);
            return true;
        case 0x4A:
            SetOsdConfigParam(rdram, ctx, runtime);
            return true;
        case 0x4B:
            GetOsdConfigParam(rdram, ctx, runtime);
            return true;
        case 0x50:
            CreateEventFlag(rdram, ctx, runtime);
            return true;
        case 0x51:
            DeleteEventFlag(rdram, ctx, runtime);
            return true;
        case 0x52:
            SetEventFlag(rdram, ctx, runtime);
            return true;
        case 0x53:
            iSetEventFlag(rdram, ctx, runtime);
            return true;
        case 0x54:
            ClearEventFlag(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x55):
            iClearEventFlag(rdram, ctx, runtime);
            return true;
        case 0x56:
            WaitEventFlag(rdram, ctx, runtime);
            return true;
        case 0x57:
            PollEventFlag(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x58):
            iPollEventFlag(rdram, ctx, runtime);
            return true;
        case 0x59:
            ReferEventFlagStatus(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x5A):
            iReferEventFlagStatus(rdram, ctx, runtime);
            return true;
        case 0x5A:
            QueryBootMode(rdram, ctx, runtime);
            return true;
        case 0x5B:
            GetThreadTLS(rdram, ctx, runtime);
            return true;
        case 0x5C:
        case static_cast<uint32_t>(-0x5C):
            EnableIntcHandler(rdram, ctx, runtime);
            return true;
        case 0x5D:
        case static_cast<uint32_t>(-0x5D):
            DisableIntcHandler(rdram, ctx, runtime);
            return true;
        case 0x5E:
        case static_cast<uint32_t>(-0x5E):
            EnableDmacHandler(rdram, ctx, runtime);
            return true;
        case 0x5F:
        case static_cast<uint32_t>(-0x5F):
            DisableDmacHandler(rdram, ctx, runtime);
            return true;
        case 0x64:
            FlushCache(rdram, ctx, runtime);
            return true;
        case 0x70:
        case static_cast<uint32_t>(-0x70):
            GsGetIMR(rdram, ctx, runtime);
            return true;
        case 0x71:
        case static_cast<uint32_t>(-0x71):
            GsPutIMR(rdram, ctx, runtime);
            return true;
        case 0x74:
            RegisterExitHandler(rdram, ctx, runtime);
            return true;
        case 0x85:
            SetMemoryMode(rdram, ctx, runtime);
            return true;
        default:
            return false;
        }
    }

    void FlushCache(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void ResetEE(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::cerr << "Syscall: ResetEE - Halting Execution (Not fully implemented)" << std::endl;
        exit(0); // Should we exit or just halt the execution?
    }

    void SetMemoryMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void CreateThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t paramAddr = getRegU32(ctx, 4); // $a0 points to ThreadParam
        const uint32_t *param = reinterpret_cast<const uint32_t *>(getConstMemPtr(rdram, paramAddr));

        if (!param)
        {
            std::cerr << "CreateThread error: invalid ThreadParam address 0x" << std::hex << paramAddr << std::dec << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        auto info = std::make_shared<ThreadInfo>();
        info->attr = param[0];
        info->entry = param[1];
        info->stack = param[2];
        info->stackSize = param[3];

        auto looksLikeGuestPtr = [](uint32_t v) -> bool
        {
            if (v == 0)
            {
                return true;
            }
            const uint32_t norm = v & 0x1FFFFFFFu;
            return norm < PS2_RAM_SIZE && norm >= 0x10000u;
        };

        auto looksLikePriority = [](uint32_t v) -> bool
        {
            // Typical EE priorities are very small integers (1..127).
            return v <= 0x400u;
        };

        const uint32_t gpA = param[4];
        const uint32_t prioA = param[5];
        const uint32_t gpB = param[5];
        const uint32_t prioB = param[4];

        // Prefer the standard EE layout (gp at +0x10, priority at +0x14),
        // but keep a fallback for callsites that used the swapped decode.
        if (looksLikeGuestPtr(gpA) && looksLikePriority(prioA))
        {
            info->gp = gpA;
            info->priority = prioA;
        }
        else if (looksLikeGuestPtr(gpB) && looksLikePriority(prioB))
        {
            info->gp = gpB;
            info->priority = prioB;
        }
        else
        {
            info->gp = gpA;
            info->priority = prioA;
        }

        info->option = param[6];
        info->currentPriority = static_cast<int>(info->priority);

        int id = 0;
        {
            std::lock_guard<std::mutex> lock(g_thread_map_mutex);
            id = g_nextThreadId++;
            g_threads[id] = info;
        }

        std::cout << "[CreateThread] id=" << id
                  << " entry=0x" << std::hex << info->entry
                  << " stack=0x" << info->stack
                  << " size=0x" << info->stackSize
                  << " gp=0x" << info->gp
                  << " prio=" << std::dec << info->priority << std::endl;

        setReturnS32(ctx, id);
    }

    void DeleteThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4)); // $a0
        std::lock_guard<std::mutex> lock(g_thread_map_mutex);
        g_threads.erase(tid);
        setReturnS32(ctx, 0);
    }

    void StartThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4)); // $a0 = thread id
        uint32_t arg = getRegU32(ctx, 5);              // $a1 = user arg

        auto info = lookupThreadInfo(tid);
        if (!info)
        {
            std::cerr << "StartThread error: unknown thread id " << tid << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->started)
            {
                setReturnS32(ctx, tid); // Already started
                return;
            }

            info->started = true;
            info->status = THS_RUN;
            info->arg = arg;
        }

        if (!runtime->hasFunction(info->entry))
        {
            std::cerr << "[StartThread] entry 0x" << std::hex << info->entry << std::dec << " is not registered" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        const uint32_t callerSp = getRegU32(ctx, 29);
        const uint32_t callerGp = getRegU32(ctx, 28);

        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->stack == 0 && info->stackSize != 0)
            {
                const uint32_t autoStack = runtime->guestMalloc(info->stackSize, 16u);
                if (autoStack != 0)
                {
                    info->stack = autoStack;
                    std::cout << "[StartThread] id=" << tid
                              << " auto-stack=0x" << std::hex << autoStack
                              << " size=0x" << info->stackSize << std::dec << std::endl;
                }
            }

            if (info->stack != 0 && info->stackSize == 0)
            {
                // Some games leave size zero in the thread param even though a stack
                // buffer is supplied; use a conservative default instead of caller SP.
                info->stackSize = 0x800u;
            }
        }

        g_activeThreads.fetch_add(1, std::memory_order_relaxed);
        std::thread([=]() mutable
                    {
            {
                std::string name = "PS2Thread_" + std::to_string(tid);
                ThreadNaming::SetCurrentThreadName(name);
            }
            R5900Context threadCtxCopy{};
            R5900Context *threadCtx = &threadCtxCopy;

            uint32_t threadSp = callerSp;
            if (info->stack)
            {
                const uint32_t stackSize = (info->stackSize != 0) ? info->stackSize : 0x800u;
                threadSp = (info->stack + stackSize) & ~0xFu;
            }
            uint32_t threadGp = info->gp;
            const uint32_t normalizedGp = threadGp & 0x1FFFFFFFu;
            if (threadGp == 0 || normalizedGp < 0x10000u || normalizedGp >= PS2_RAM_SIZE)
            {
                threadGp = callerGp;
            }

            SET_GPR_U32(threadCtx, 29, threadSp);
            SET_GPR_U32(threadCtx, 28, threadGp);
            SET_GPR_U32(threadCtx, 4, info->arg);
            SET_GPR_U32(threadCtx, 31, 0);
            threadCtx->pc = info->entry;

            PS2Runtime::RecompiledFunction func = runtime->lookupFunction(info->entry);
            g_currentThreadId = tid;

            std::cout << "[StartThread] id=" << tid
                      << " entry=0x" << std::hex << info->entry
                      << " sp=0x" << GPR_U32(threadCtx, 29)
                      << " gp=0x" << GPR_U32(threadCtx, 28)
                      << " arg=0x" << info->arg << std::dec << std::endl;

            bool exited = false;
            try
            {
                func(rdram, threadCtx, runtime);
            }
            catch (const ThreadExitException &)
            {
                exited = true;
            }
            catch (const std::exception &e)
            {
                std::cerr << "[StartThread] id=" << tid << " exception: " << e.what() << std::endl;
            }

            if (!exited)
            {
                std::cout << "[StartThread] id=" << tid << " returned (pc=0x"
                          << std::hex << threadCtx->pc << std::dec << ")" << std::endl;
            }

            runExitHandlersForThread(tid, rdram, threadCtx, runtime);

            {
                std::lock_guard<std::mutex> lock(info->m);
                info->started = false;
                info->status = THS_DORMANT;
            }

            g_activeThreads.fetch_sub(1, std::memory_order_relaxed); })
            .detach();

        // for now report success to the caller.
        setReturnS32(ctx, tid);
    }

    void ExitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        runExitHandlersForThread(g_currentThreadId, rdram, ctx, runtime);
        auto info = ensureCurrentThreadInfo(ctx);
        if (info)
        {
            std::lock_guard<std::mutex> lock(info->m);
            info->terminated = true;
            info->forceRelease = true;
            info->status = THS_DORMANT;
            info->waitType = TSW_NONE;
            info->waitId = 0;
            info->wakeupCount = 0;
        }
        if (info)
        {
            info->cv.notify_all();
        }
        throw ThreadExitException();
    }

    void ExitDeleteThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = g_currentThreadId;
        runExitHandlersForThread(tid, rdram, ctx, runtime);
        auto info = ensureCurrentThreadInfo(ctx);
        if (info)
        {
            std::lock_guard<std::mutex> lock(info->m);
            info->terminated = true;
            info->forceRelease = true;
            info->status = THS_DORMANT;
            info->waitType = TSW_NONE;
            info->waitId = 0;
            info->wakeupCount = 0;
        }
        if (info)
        {
            info->cv.notify_all();
        }
        {
            std::lock_guard<std::mutex> lock(g_thread_map_mutex);
            g_threads.erase(tid);
        }
        throw ThreadExitException();
    }

    void TerminateThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (tid == 0)
            tid = g_currentThreadId;

        auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, -1);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(info->m);
            info->terminated = true;
            info->forceRelease = true;
            info->status = THS_DORMANT;
            info->waitType = TSW_NONE;
            info->waitId = 0;
            info->wakeupCount = 0;
        }
        info->cv.notify_all();

        if (tid == g_currentThreadId)
        {
            runExitHandlersForThread(tid, rdram, ctx, runtime);
            throw ThreadExitException();
        }
        setReturnS32(ctx, 0);
    }

    void SuspendThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (tid == 0)
            tid = g_currentThreadId;

        auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, -1);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->status == THS_DORMANT)
            {
                setReturnS32(ctx, -1);
                return;
            }
            info->suspendCount++;
            applySuspendStatusLocked(*info);
        }
        info->cv.notify_all();

        if (tid == g_currentThreadId)
        {
            std::unique_lock<std::mutex> lock(info->m);
            info->cv.wait(lock, [&]()
                          { return info->suspendCount == 0 || info->terminated.load(); });
            if (info->terminated.load())
            {
                throw ThreadExitException();
            }
            info->status = THS_RUN;
        }

        setReturnS32(ctx, 0);
    }

    void ResumeThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (tid == 0)
            tid = g_currentThreadId;

        auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, -1);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->suspendCount <= 0)
            {
                setReturnS32(ctx, -1);
                return;
            }
            info->suspendCount--;
            if (info->suspendCount == 0)
            {
                if (info->waitType != TSW_NONE)
                {
                    info->status = THS_WAIT;
                }
                else
                {
                    info->status = (tid == g_currentThreadId) ? THS_RUN : THS_READY;
                }
            }
        }
        info->cv.notify_all();
        setReturnS32(ctx, 0);
    }

    void GetThreadId(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, g_currentThreadId);
    }

    void ReferThreadStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        uint32_t statusAddr = getRegU32(ctx, 5);

        if (tid == 0) // TH_SELF
        {
            tid = g_currentThreadId;
        }

        auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, -1);
            return;
        }

        ee_thread_status_t *status = reinterpret_cast<ee_thread_status_t *>(getMemPtr(rdram, statusAddr));
        if (!status)
        {
            setReturnS32(ctx, -1);
            return;
        }

        std::lock_guard<std::mutex> lock(info->m);
        status->status = info->status;
        status->func = info->entry;
        status->stack = info->stack;
        status->stack_size = info->stackSize;
        status->gp_reg = info->gp;
        status->initial_priority = info->priority;
        status->current_priority = info->currentPriority;
        status->attr = info->attr;
        status->option = info->option;
        status->waitType = info->waitType;
        status->waitId = info->waitId;
        status->wakeupCount = info->wakeupCount;
        setReturnS32(ctx, 0);
    }

    void SleepThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        auto info = ensureCurrentThreadInfo(ctx);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        throwIfTerminated(info);

        int ret = 0;
        std::unique_lock<std::mutex> lock(info->m);

        if (info->wakeupCount > 0)
        {
            info->wakeupCount--;
            info->status = THS_RUN;
            info->waitType = TSW_NONE;
            info->waitId = 0;
            ret = 0;
        }
        else
        {
            info->status = THS_WAIT;
            info->waitType = TSW_SLEEP;
            info->waitId = 0;
            info->forceRelease = false;

            info->cv.wait(lock, [&]()
                          { return info->wakeupCount > 0 || info->forceRelease.load() || info->terminated.load(); });

            if (info->terminated.load())
            {
                throw ThreadExitException();
            }

            info->status = THS_RUN;
            info->waitType = TSW_NONE;
            info->waitId = 0;

            if (info->forceRelease.load())
            {
                info->forceRelease = false;
                ret = KE_RELEASE_WAIT;
            }
            else
            {
                if (info->wakeupCount > 0)
                    info->wakeupCount--;
                ret = 0;
            }
        }

        lock.unlock();
        waitWhileSuspended(info);
        setReturnS32(ctx, ret);
    }

    void WakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (tid == 0)
        {
            setReturnS32(ctx, KE_ILLEGAL_THID);
            return;
        }

        auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->status == THS_DORMANT)
            {
                setReturnS32(ctx, KE_DORMANT);
                return;
            }
            if (info->status == THS_WAIT && info->waitType == TSW_SLEEP)
            {
                if (info->suspendCount > 0)
                {
                    info->status = THS_SUSPEND;
                }
                else
                {
                    info->status = THS_READY;
                }
                info->waitType = TSW_NONE;
                info->waitId = 0;
                info->wakeupCount++;
                info->cv.notify_one();
            }
            else
            {
                info->wakeupCount++;
            }
        }
        setReturnS32(ctx, 0);
    }

    void iWakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        WakeupThread(rdram, ctx, runtime);
    }

    void CancelWakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (tid == 0)
            tid = g_currentThreadId;

        auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, -1);
            return;
        }

        int previous = 0;
        {
            std::lock_guard<std::mutex> lock(info->m);
            previous = info->wakeupCount;
            info->wakeupCount = 0;
        }
        setReturnS32(ctx, previous);
    }

    void iCancelWakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (tid == 0)
        {
            setReturnS32(ctx, KE_ILLEGAL_THID);
            return;
        }

        auto info = lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        int previous = 0;
        {
            std::lock_guard<std::mutex> lock(info->m);
            previous = info->wakeupCount;
            info->wakeupCount = 0;
        }
        setReturnS32(ctx, previous);
    }

    void ChangeThreadPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        int newPrio = static_cast<int>(getRegU32(ctx, 5));

        if (tid == 0)
            tid = g_currentThreadId;

        auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
        if (info)
        {
            int oldPrio = info->currentPriority;
            info->currentPriority = newPrio;
            setReturnS32(ctx, oldPrio); // Return old priority?
        }
        else
        {
            setReturnS32(ctx, -1);
        }
    }

    void RotateThreadReadyQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        int prio = static_cast<int>(getRegU32(ctx, 4));
        if (logCount < 16)
        {
            std::cout << "[RotateThreadReadyQueue] prio=" << prio << std::endl;
            ++logCount;
        }
        if (prio >= 128)
        {
            setReturnS32(ctx, -1);
            return;
        }
        setReturnS32(ctx, 0);
    }

    void ReleaseWaitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (tid == 0)
        {
            setReturnS32(ctx, KE_ILLEGAL_THID);
            return;
        }

        auto info = lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        bool wasWaiting = false;
        int waitType = 0;
        int waitId = 0;

        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->status == THS_WAIT)
            {
                wasWaiting = true;
                waitType = info->waitType;
                waitId = info->waitId;
                info->forceRelease = true;
                info->waitType = TSW_NONE;
                info->waitId = 0;
                if (info->suspendCount > 0)
                {
                    info->status = THS_SUSPEND;
                }
                else
                {
                    info->status = THS_READY;
                }
            }
        }

        if (!wasWaiting)
        {
            setReturnS32(ctx, KE_NOT_WAIT);
            return;
        }

        info->cv.notify_all();

        if (waitType == TSW_SEMA)
        {
            auto sema = lookupSemaInfo(waitId);
            if (sema)
            {
                sema->cv.notify_all();
            }
        }
        else if (waitType == TSW_EVENT)
        {
            auto eventFlag = lookupEventFlagInfo(waitId);
            if (eventFlag)
            {
                eventFlag->cv.notify_all();
            }
        }
        setReturnS32(ctx, 0);
    }

    void iReleaseWaitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ReleaseWaitThread(rdram, ctx, runtime);
    }

    void CreateSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t paramAddr = getRegU32(ctx, 4); // $a0
        const uint32_t *param = reinterpret_cast<const uint32_t *>(getConstMemPtr(rdram, paramAddr));
        int init = 0;
        int max = 1;
        uint32_t attr = 0;
        uint32_t option = 0;

        if (param)
        {
            // sceSemaParam layout commonly: attr(0), option(1), initCount(2), maxCount(3)
            attr = param[0];
            option = param[1];
            init = static_cast<int>(param[2]);
            max = static_cast<int>(param[3]);
        }
        if (max <= 0)
        {
            max = 1;
        }
        if (init > max)
        {
            init = max;
        }

        int id = 0;
        auto info = std::make_shared<SemaInfo>();
        info->count = init;
        info->maxCount = max;
        info->initCount = init;
        info->attr = attr;
        info->option = option;

        {
            std::lock_guard<std::mutex> lock(g_sema_map_mutex);
            id = g_nextSemaId++;
            g_semas.emplace(id, info);
        }
        std::cout << "[CreateSema] id=" << id << " init=" << init << " max=" << max << std::endl;
        setReturnS32(ctx, id);
    }

    void DeleteSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int sid = static_cast<int>(getRegU32(ctx, 4));
        std::lock_guard<std::mutex> lock(g_sema_map_mutex);
        g_semas.erase(sid);
        setReturnS32(ctx, 0);
    }

    void SignalSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int sid = static_cast<int>(getRegU32(ctx, 4));
        auto sema = lookupSemaInfo(sid);
        if (sema)
        {
            std::lock_guard<std::mutex> lock(sema->m);
            if (sema->count < sema->maxCount)
            {
                sema->count++;
            }
            sema->cv.notify_one();
        }
        setReturnS32(ctx, 0);
    }

    void iSignalSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        SignalSema(rdram, ctx, runtime);
    }

    void WaitSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int sid = static_cast<int>(getRegU32(ctx, 4));
        auto sema = lookupSemaInfo(sid);
        if (!sema)
        {
            setReturnS32(ctx, KE_UNKNOWN_SEMID);
            return;
        }

        auto info = ensureCurrentThreadInfo(ctx);
        throwIfTerminated(info);
        std::unique_lock<std::mutex> lock(sema->m);
        int ret = 0;

        if (sema->count == 0)
        {
            if (info)
            {
                std::lock_guard<std::mutex> tLock(info->m);
                info->status = THS_WAIT;
                info->waitType = TSW_SEMA;
                info->waitId = sid;
                info->forceRelease = false;
            }

            sema->waiters++;
            sema->cv.wait(lock, [&]()
                          { 
                              bool forced = info ? info->forceRelease.load() : false;
                              bool terminated = info ? info->terminated.load() : false;
                              return sema->count > 0 || forced || terminated; });
            sema->waiters--;

            if (info)
            {
                std::lock_guard<std::mutex> tLock(info->m);
                info->status = THS_RUN;
                info->waitType = TSW_NONE;
                info->waitId = 0;
                if (info->forceRelease)
                {
                    info->forceRelease = false;
                    ret = KE_RELEASE_WAIT;
                }
            }

            if (info && info->terminated.load())
            {
                throw ThreadExitException();
            }
        }

        if (ret == 0 && sema->count > 0)
        {
            sema->count--;
        }
        lock.unlock();
        waitWhileSuspended(info);
        setReturnS32(ctx, ret);
    }

    void PollSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int sid = static_cast<int>(getRegU32(ctx, 4));
        auto sema = lookupSemaInfo(sid);
        if (!sema)
        {
            setReturnS32(ctx, KE_UNKNOWN_SEMID);
            return;
        }

        std::lock_guard<std::mutex> lock(sema->m);
        if (sema->count > 0)
        {
            sema->count--;
            setReturnS32(ctx, KE_OK);
            return;
        }

        setReturnS32(ctx, KE_SEMA_ZERO);
    }

    void iPollSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        PollSema(rdram, ctx, runtime);
    }

    void ReferSemaStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int sid = static_cast<int>(getRegU32(ctx, 4));
        uint32_t statusAddr = getRegU32(ctx, 5);

        auto sema = lookupSemaInfo(sid);
        if (!sema)
        {
            setReturnS32(ctx, -1);
            return;
        }

        ee_sema_t *status = reinterpret_cast<ee_sema_t *>(getMemPtr(rdram, statusAddr));
        if (!status)
        {
            setReturnS32(ctx, -1);
            return;
        }

        std::lock_guard<std::mutex> lock(sema->m);
        status->count = sema->count;
        status->max_count = sema->maxCount;
        status->init_count = sema->initCount;
        status->wait_threads = sema->waiters;
        status->attr = sema->attr;
        status->option = sema->option;
        setReturnS32(ctx, 0);
    }

    void iReferSemaStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ReferSemaStatus(rdram, ctx, runtime);
    }

    constexpr uint32_t WEF_OR = 1;
    constexpr uint32_t WEF_CLEAR = 0x10;
    constexpr uint32_t WEF_CLEAR_ALL = 0x20;
    constexpr uint32_t WEF_MODE_MASK = WEF_OR | WEF_CLEAR | WEF_CLEAR_ALL;
    constexpr uint32_t EA_MULTI = 0x2;

    void CreateEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t paramAddr = getRegU32(ctx, 4); // $a0
        const uint32_t *param = reinterpret_cast<const uint32_t *>(getConstMemPtr(rdram, paramAddr));

        auto info = std::make_shared<EventFlagInfo>();
        if (param)
        {
            info->attr = param[0];
            info->option = param[1];
            info->initBits = param[2];
            info->bits = info->initBits;
        }

        int id = 0;
        {
            std::lock_guard<std::mutex> mapLock(g_event_flag_map_mutex);
            id = g_nextEventFlagId++;
            g_eventFlags[id] = info;
        }
        setReturnS32(ctx, id);
    }

    void DeleteEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int eid = static_cast<int>(getRegU32(ctx, 4));
        std::shared_ptr<EventFlagInfo> info;
        {
            std::lock_guard<std::mutex> mapLock(g_event_flag_map_mutex);
            auto it = g_eventFlags.find(eid);
            if (it == g_eventFlags.end())
            {
                setReturnS32(ctx, KE_UNKNOWN_EVFID);
                return;
            }
            info = it->second;
            g_eventFlags.erase(it);
        }

        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_EVFID);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(info->m);
            info->deleted = true;
        }
        info->cv.notify_all();
        setReturnS32(ctx, 0);
    }

    void SetEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int eid = static_cast<int>(getRegU32(ctx, 4));
        uint32_t bits = getRegU32(ctx, 5);
        auto info = lookupEventFlagInfo(eid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_EVFID);
            return;
        }

        if (bits == 0)
        {
            setReturnS32(ctx, KE_OK);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(info->m);
            info->bits |= bits;
        }
        info->cv.notify_all();
        setReturnS32(ctx, 0);
    }

    void iSetEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        SetEventFlag(rdram, ctx, runtime);
    }

    void ClearEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int eid = static_cast<int>(getRegU32(ctx, 4));
        uint32_t bits = getRegU32(ctx, 5);
        auto info = lookupEventFlagInfo(eid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_EVFID);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(info->m);
            info->bits &= bits;
        }
        info->cv.notify_all();
        setReturnS32(ctx, KE_OK);
    }

    void iClearEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ClearEventFlag(rdram, ctx, runtime);
    }

    void WaitEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int eid = static_cast<int>(getRegU32(ctx, 4));
        uint32_t waitBits = getRegU32(ctx, 5);
        uint32_t mode = getRegU32(ctx, 6);
        uint32_t resBitsAddr = getRegU32(ctx, 7);

        if ((mode & ~WEF_MODE_MASK) != 0)
        {
            setReturnS32(ctx, KE_ILLEGAL_MODE);
            return;
        }

        if (waitBits == 0)
        {
            setReturnS32(ctx, KE_EVF_ILPAT);
            return;
        }

        auto info = lookupEventFlagInfo(eid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_EVFID);
            return;
        }

        uint32_t *resBitsPtr = resBitsAddr ? reinterpret_cast<uint32_t *>(getMemPtr(rdram, resBitsAddr)) : nullptr;

        std::unique_lock<std::mutex> lock(info->m);
        if ((info->attr & EA_MULTI) == 0 && info->waiters > 0)
        {
            setReturnS32(ctx, KE_EVF_MULTI);
            return;
        }

        auto tInfo = ensureCurrentThreadInfo(ctx);
        throwIfTerminated(tInfo);
        int ret = KE_OK;

        auto satisfied = [&]()
        {
            if (tInfo && tInfo->forceRelease.load())
                return true;
            if (tInfo && tInfo->terminated.load())
                return true;
            if (info->deleted)
            {
                return true;
            }
            if (mode & WEF_OR)
            {
                return (info->bits & waitBits) != 0;
            }
            return (info->bits & waitBits) == waitBits;
        };

        if (!satisfied())
        {
            if (tInfo)
            {
                std::lock_guard<std::mutex> tLock(tInfo->m);
                tInfo->status = THS_WAIT;
                tInfo->waitType = TSW_EVENT;
                tInfo->waitId = eid;
                tInfo->forceRelease = false;
            }

            info->waiters++;
            info->cv.wait(lock, satisfied);
            info->waiters--;

            if (tInfo)
            {
                std::lock_guard<std::mutex> tLock(tInfo->m);
                tInfo->status = THS_RUN;
                tInfo->waitType = TSW_NONE;
                tInfo->waitId = 0;
                if (tInfo->forceRelease)
                {
                    tInfo->forceRelease = false;
                    ret = KE_RELEASE_WAIT;
                }
            }

            if (tInfo && tInfo->terminated.load())
            {
                throw ThreadExitException();
            }
        }

        if (ret == KE_OK && info->deleted)
        {
            ret = KE_WAIT_DELETE;
        }

        if (ret == KE_OK && resBitsPtr)
        {
            *resBitsPtr = info->bits;
        }

        if (ret == KE_OK && (mode & (WEF_CLEAR | WEF_CLEAR_ALL)))
        {
            info->bits = 0;
        }

        lock.unlock();
        waitWhileSuspended(tInfo);
        setReturnS32(ctx, ret);
    }

    void PollEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int eid = static_cast<int>(getRegU32(ctx, 4));
        uint32_t waitBits = getRegU32(ctx, 5);
        uint32_t mode = getRegU32(ctx, 6);
        uint32_t resBitsAddr = getRegU32(ctx, 7);

        if ((mode & ~WEF_MODE_MASK) != 0)
        {
            setReturnS32(ctx, KE_ILLEGAL_MODE);
            return;
        }

        if (waitBits == 0)
        {
            setReturnS32(ctx, KE_EVF_ILPAT);
            return;
        }

        auto info = lookupEventFlagInfo(eid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_EVFID);
            return;
        }

        uint32_t *resBitsPtr = resBitsAddr ? reinterpret_cast<uint32_t *>(getMemPtr(rdram, resBitsAddr)) : nullptr;

        std::lock_guard<std::mutex> lock(info->m);
        if ((info->attr & EA_MULTI) == 0 && info->waiters > 0)
        {
            setReturnS32(ctx, KE_EVF_MULTI);
            return;
        }

        bool ok = false;
        if (mode & WEF_OR)
        {
            ok = (info->bits & waitBits) != 0;
        }
        else
        {
            ok = (info->bits & waitBits) == waitBits;
        }

        if (!ok)
        {
            setReturnS32(ctx, KE_EVF_COND);
            return;
        }

        if (resBitsPtr)
        {
            *resBitsPtr = info->bits;
        }

        if (mode & (WEF_CLEAR | WEF_CLEAR_ALL))
        {
            info->bits = 0;
        }

        setReturnS32(ctx, KE_OK);
    }

    void iPollEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        PollEventFlag(rdram, ctx, runtime);
    }

    void ReferEventFlagStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int eid = static_cast<int>(getRegU32(ctx, 4));
        uint32_t infoAddr = getRegU32(ctx, 5);

        struct Ps2EventFlagInfo
        {
            uint32_t attr;
            uint32_t option;
            uint32_t initBits;
            uint32_t currBits;
            int32_t numThreads;
            int32_t reserved1;
            int32_t reserved2;
        };

        auto info = lookupEventFlagInfo(eid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_EVFID);
            return;
        }

        Ps2EventFlagInfo *out = infoAddr ? reinterpret_cast<Ps2EventFlagInfo *>(getMemPtr(rdram, infoAddr)) : nullptr;
        if (!out)
        {
            setReturnS32(ctx, -1);
            return;
        }

        std::lock_guard<std::mutex> lock(info->m);
        out->attr = info->attr;
        out->option = info->option;
        out->initBits = info->initBits;
        out->currBits = info->bits;
        out->numThreads = info->waiters;
        out->reserved1 = 0;
        out->reserved2 = 0;
        setReturnS32(ctx, 0);
    }

    void iReferEventFlagStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ReferEventFlagStatus(rdram, ctx, runtime);
    }

    void SetAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint16_t ticks = static_cast<uint16_t>(getRegU32(ctx, 4) & 0xFFFFu);
        uint32_t handler = getRegU32(ctx, 5);
        uint32_t arg = getRegU32(ctx, 6);

        static int logCount = 0;
        if (logCount < 5)
        {
            std::cout << "[SetAlarm] ticks=" << ticks
                      << " handler=0x" << std::hex << handler
                      << " arg=0x" << arg << std::dec << std::endl;
            ++logCount;
        }

        if (!runtime || !handler || !runtime->hasFunction(handler))
        {
            setReturnS32(ctx, KE_ERROR);
            return;
        }

        auto info = std::make_shared<AlarmInfo>();
        info->ticks = ticks;
        info->handler = handler;
        info->commonArg = arg;
        info->gp = getRegU32(ctx, 28);
        info->sp = getRegU32(ctx, 29);
        info->rdram = rdram;
        info->runtime = runtime;
        info->dueAt = std::chrono::steady_clock::now() + alarmTicksToDuration(ticks);

        int alarmId = 0;
        {
            std::lock_guard<std::mutex> lock(g_alarm_mutex);
            alarmId = g_nextAlarmId++;
            if (g_nextAlarmId <= 0)
            {
                g_nextAlarmId = 1;
            }
            info->id = alarmId;
            g_alarms[alarmId] = info;
        }

        ensureAlarmWorkerRunning();
        g_alarm_cv.notify_all();
        setReturnS32(ctx, alarmId);
    }

    void iSetAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        SetAlarm(rdram, ctx, runtime);
    }

    void CancelAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int alarmId = static_cast<int>(getRegU32(ctx, 4));
        if (alarmId <= 0)
        {
            setReturnS32(ctx, KE_ERROR);
            return;
        }

        bool removed = false;
        {
            std::lock_guard<std::mutex> lock(g_alarm_mutex);
            removed = g_alarms.erase(alarmId) != 0;
        }

        if (removed)
        {
            g_alarm_cv.notify_all();
            setReturnS32(ctx, KE_OK);
            return;
        }

        setReturnS32(ctx, KE_ERROR);
    }

    void iCancelAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        CancelAlarm(rdram, ctx, runtime);
    }

    void EnableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void DisableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void AddIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        IrqHandlerInfo info{};
        info.cause = getRegU32(ctx, 4);
        info.handler = getRegU32(ctx, 5);
        info.arg = getRegU32(ctx, 6);
        info.enabled = true;

        const int handlerId = g_nextIntcHandlerId++;
        g_intcHandlers[handlerId] = info;
        setReturnS32(ctx, handlerId);
    }

    void RemoveIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int handlerId = static_cast<int>(getRegU32(ctx, 5));
        if (handlerId > 0)
        {
            g_intcHandlers.erase(handlerId);
        }
        setReturnS32(ctx, 0);
    }

    void AddDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        IrqHandlerInfo info{};
        info.cause = getRegU32(ctx, 4);
        info.handler = getRegU32(ctx, 5);
        info.arg = getRegU32(ctx, 6);
        info.enabled = true;

        const int handlerId = g_nextDmacHandlerId++;
        g_dmacHandlers[handlerId] = info;
        setReturnS32(ctx, handlerId);
    }

    void RemoveDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int handlerId = static_cast<int>(getRegU32(ctx, 5));
        if (handlerId > 0)
        {
            g_dmacHandlers.erase(handlerId);
        }
        setReturnS32(ctx, 0);
    }

    void EnableIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int handlerId = static_cast<int>(getRegU32(ctx, 5));
        if (auto it = g_intcHandlers.find(handlerId); it != g_intcHandlers.end())
        {
            it->second.enabled = true;
        }
        setReturnS32(ctx, 0);
    }

    void DisableIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int handlerId = static_cast<int>(getRegU32(ctx, 5));
        if (auto it = g_intcHandlers.find(handlerId); it != g_intcHandlers.end())
        {
            it->second.enabled = false;
        }
        setReturnS32(ctx, 0);
    }

    void EnableDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int handlerId = static_cast<int>(getRegU32(ctx, 5));
        if (auto it = g_dmacHandlers.find(handlerId); it != g_dmacHandlers.end())
        {
            it->second.enabled = true;
        }
        setReturnS32(ctx, 0);
    }

    void DisableDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int handlerId = static_cast<int>(getRegU32(ctx, 5));
        if (auto it = g_dmacHandlers.find(handlerId); it != g_dmacHandlers.end())
        {
            it->second.enabled = false;
        }
        setReturnS32(ctx, 0);
    }

    void EnableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void DisableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void SifStopModule(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int32_t moduleId = static_cast<int32_t>(getRegU32(ctx, 4)); // $a0
        const uint32_t resultAddr = getRegU32(ctx, 7);                    // $a3 (int* result, optional)

        uint32_t refsLeft = 0;
        const bool knownModule = trackSifModuleStop(moduleId, &refsLeft);
        const int32_t ret = knownModule ? 0 : -1;

        if (resultAddr != 0)
        {
            int32_t *hostResult = reinterpret_cast<int32_t *>(getMemPtr(rdram, resultAddr));
            if (hostResult)
            {
                *hostResult = knownModule ? 0 : -1;
            }
        }

        if (knownModule)
        {
            std::string modulePath;
            {
                std::lock_guard<std::mutex> lock(g_sif_module_mutex);
                auto it = g_sif_modules_by_id.find(moduleId);
                if (it != g_sif_modules_by_id.end())
                {
                    modulePath = it->second.path;
                }
            }
            logSifModuleAction("stop", moduleId, modulePath, refsLeft);
        }

        setReturnS32(ctx, ret);
    }

    void SifLoadModule(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t pathAddr = getRegU32(ctx, 4); // $a0
        const std::string modulePath = readGuestCStringBounded(rdram, pathAddr, kMaxSifModulePathBytes);
        if (modulePath.empty())
        {
            setReturnS32(ctx, -1);
            return;
        }

        const int32_t moduleId = trackSifModuleLoad(modulePath);
        if (moduleId <= 0)
        {
            setReturnS32(ctx, -1);
            return;
        }

        uint32_t refs = 0;
        {
            std::lock_guard<std::mutex> lock(g_sif_module_mutex);
            auto it = g_sif_modules_by_id.find(moduleId);
            if (it != g_sif_modules_by_id.end())
            {
                refs = it->second.refCount;
            }
        }
        logSifModuleAction("load", moduleId, modulePath, refs);

        setReturnS32(ctx, moduleId);
    }

    void SifInitRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::lock_guard<std::mutex> lock(g_rpc_mutex);
        if (!g_rpc_initialized)
        {
            g_rpc_servers.clear();
            g_rpc_clients.clear();
            g_rpc_next_id = 1;
            g_rpc_packet_index = 0;
            g_rpc_server_index = 0;
            g_rpc_active_queue = 0;
            {
                std::lock_guard<std::mutex> dtxLock(g_dtx_rpc_mutex);
                g_dtx_remote_by_id.clear();
                g_dtx_next_urpc_obj = kDtxUrpcObjBase;
            }
            g_rpc_initialized = true;
            std::cout << "[SifInitRpc] Initialized" << std::endl;
        }
        setReturnS32(ctx, 0);
    }

    void SifBindRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t clientPtr = getRegU32(ctx, 4);
        uint32_t rpcId = getRegU32(ctx, 5);
        uint32_t mode = getRegU32(ctx, 6);

        t_SifRpcClientData *client = reinterpret_cast<t_SifRpcClientData *>(getMemPtr(rdram, clientPtr));

        if (!client)
        {
            setReturnS32(ctx, -1);
            return;
        }

        client->command = 0;
        client->buf = 0;
        client->cbuf = 0;
        client->end_function = 0;
        client->end_param = 0;
        client->server = 0;
        client->hdr.pkt_addr = 0;
        client->hdr.sema_id = -1;
        client->hdr.mode = mode;

        uint32_t serverPtr = 0;
        {
            std::lock_guard<std::mutex> lock(g_rpc_mutex);
            client->hdr.rpc_id = g_rpc_next_id++;
            auto it = g_rpc_servers.find(rpcId);
            if (it != g_rpc_servers.end())
            {
                serverPtr = it->second.sd_ptr;
            }
            g_rpc_clients[clientPtr] = {};
            g_rpc_clients[clientPtr].sid = rpcId;
        }

        if (!serverPtr)
        {
            // Allocate a dummy server so bind loops can proceed.
            serverPtr = rpcAllocServerAddr(rdram);
            if (serverPtr)
            {
                t_SifRpcServerData *dummy = reinterpret_cast<t_SifRpcServerData *>(getMemPtr(rdram, serverPtr));
                if (dummy)
                {
                    std::memset(dummy, 0, sizeof(*dummy));
                    dummy->sid = static_cast<int>(rpcId);
                }
                std::lock_guard<std::mutex> lock(g_rpc_mutex);
                g_rpc_servers[rpcId] = {rpcId, serverPtr};
            }
        }

        if (serverPtr)
        {
            t_SifRpcServerData *sd = reinterpret_cast<t_SifRpcServerData *>(getMemPtr(rdram, serverPtr));
            client->server = serverPtr;
            client->buf = sd ? sd->buf : 0;
            client->cbuf = sd ? sd->cbuf : 0;
        }
        else
        {
            client->server = 0;
            client->buf = 0;
            client->cbuf = 0;
        }

        setReturnS32(ctx, 0);
    }

    void SifCallRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t clientPtr = getRegU32(ctx, 4);
        uint32_t rpcNum = getRegU32(ctx, 5);
        uint32_t mode = getRegU32(ctx, 6);
        uint32_t sendBuf = getRegU32(ctx, 7);
        uint32_t sendSize = 0;
        uint32_t recvBuf = 0;
        uint32_t recvSize = 0;
        uint32_t endFunc = 0;
        uint32_t endParam = 0;

        // EE-side calls use extended arg registers:
        // a0-a3 => r4-r7, arg5-arg8 => r8-r11, arg9 => stack + 0x0.
        // Keep O32 stack-layout fallback for compatibility with other call sites.
        uint32_t sp = getRegU32(ctx, 29);
        sendSize = getRegU32(ctx, 8);
        recvBuf = getRegU32(ctx, 9);
        recvSize = getRegU32(ctx, 10);
        endFunc = getRegU32(ctx, 11);
        (void)readStackU32(rdram, sp, 0x0, endParam);

        if (sendSize == 0 && recvBuf == 0 && recvSize == 0 && endFunc == 0)
        {
            readStackU32(rdram, sp, 0x10, sendSize);
            readStackU32(rdram, sp, 0x14, recvBuf);
            readStackU32(rdram, sp, 0x18, recvSize);
            readStackU32(rdram, sp, 0x1C, endFunc);
            readStackU32(rdram, sp, 0x20, endParam);
        }

        t_SifRpcClientData *client = reinterpret_cast<t_SifRpcClientData *>(getMemPtr(rdram, clientPtr));

        if (!client)
        {
            setReturnS32(ctx, -1);
            return;
        }

        client->command = rpcNum;
        client->end_function = endFunc;
        client->end_param = endParam;
        client->hdr.mode = mode;

        {
            std::lock_guard<std::mutex> lock(g_rpc_mutex);
            g_rpc_clients[clientPtr].busy = true;
            g_rpc_clients[clientPtr].last_rpc = rpcNum;
            uint32_t sid = g_rpc_clients[clientPtr].sid;
            if (sid)
            {
                auto it = g_rpc_servers.find(sid);
                if (it != g_rpc_servers.end())
                {
                    uint32_t mappedServer = it->second.sd_ptr;
                    if (mappedServer && client->server != mappedServer)
                    {
                        client->server = mappedServer;
                    }
                }
            }
        }

        uint32_t sid = 0;
        {
            std::lock_guard<std::mutex> lock(g_rpc_mutex);
            auto it = g_rpc_clients.find(clientPtr);
            if (it != g_rpc_clients.end())
            {
                sid = it->second.sid;
            }
        }

        uint32_t serverPtr = client->server;
        t_SifRpcServerData *sd = serverPtr ? reinterpret_cast<t_SifRpcServerData *>(getMemPtr(rdram, serverPtr)) : nullptr;

        if (sd)
        {
            sd->client = clientPtr;
            sd->pkt_addr = client->hdr.pkt_addr;
            sd->rpc_number = rpcNum;
            sd->size = static_cast<int>(sendSize);
            sd->recvbuf = recvBuf;
            sd->rsize = static_cast<int>(recvSize);
            sd->rmode = ((mode & kSifRpcModeNowait) && endFunc == 0) ? 0 : 1;
            sd->rid = 0;
        }

        if (sd && sd->buf && sendBuf && sendSize > 0)
        {
            rpcCopyToRdram(rdram, sd->buf, sendBuf, sendSize);
        }

        uint32_t resultPtr = 0;
        bool handled = false;

        auto readRpcU32 = [&](uint32_t addr, uint32_t &out) -> bool
        {
            if (!addr)
            {
                return false;
            }
            const uint8_t *ptr = getConstMemPtr(rdram, addr);
            if (!ptr)
            {
                return false;
            }
            std::memcpy(&out, ptr, sizeof(out));
            return true;
        };

        auto writeRpcU32 = [&](uint32_t addr, uint32_t value) -> bool
        {
            if (!addr)
            {
                return false;
            }
            uint8_t *ptr = getMemPtr(rdram, addr);
            if (!ptr)
            {
                return false;
            }
            std::memcpy(ptr, &value, sizeof(value));
            return true;
        };

        const bool isDtxUrpc = (sid == kDtxRpcSid) && (rpcNum >= 0x400u) && (rpcNum < 0x500u);
        uint32_t dtxUrpcCommand = isDtxUrpc ? (rpcNum & 0xFFu) : 0u;
        uint32_t dtxUrpcFn = 0;
        uint32_t dtxUrpcObj = 0;
        uint32_t dtxUrpcSend0 = 0;
        bool dtxUrpcDispatchAttempted = false;
        bool dtxUrpcFallbackEmulated = false;
        bool dtxUrpcFallbackCreate34 = false;
        bool hasUrpcHandler = false;
        if (isDtxUrpc)
        {
            if (sendBuf && sendSize >= sizeof(uint32_t))
            {
                (void)readRpcU32(sendBuf, dtxUrpcSend0);
            }
            if (dtxUrpcCommand < 64u)
            {
                (void)readRpcU32(kDtxUrpcFnTableBase + (dtxUrpcCommand * 4u), dtxUrpcFn);
                (void)readRpcU32(kDtxUrpcObjTableBase + (dtxUrpcCommand * 4u), dtxUrpcObj);
            }
            hasUrpcHandler = (dtxUrpcCommand < 64u) && (dtxUrpcFn != 0u);
        }
        const bool allowServerDispatch = !isDtxUrpc || hasUrpcHandler;

        if (sd && sd->func && (sid != kDtxRpcSid || isDtxUrpc) && allowServerDispatch)
        {
            dtxUrpcDispatchAttempted = dtxUrpcDispatchAttempted || isDtxUrpc;
            handled = rpcInvokeFunction(rdram, ctx, runtime, sd->func, rpcNum, sd->buf, sendSize, 0, &resultPtr);
            if (handled && resultPtr == 0 && sd->buf)
            {
                resultPtr = sd->buf;
            }
            if (handled && resultPtr == 0 && recvBuf)
            {
                resultPtr = recvBuf;
            }
        }

        if (!handled && isDtxUrpc && sendBuf && sendSize > 0)
        {
            // Only dispatch through dtx_rpc_func when a URPC handler is registered in the table.
            // If the slot is empty, defer to the fallback emulation below.
            if (hasUrpcHandler)
            {
                dtxUrpcDispatchAttempted = true;
                handled = rpcInvokeFunction(rdram, ctx, runtime, 0x2fabc0u, rpcNum, sendBuf, sendSize, 0, &resultPtr);
                if (handled && resultPtr == 0)
                {
                    resultPtr = sendBuf;
                }
            }
        }

        if (!handled && sid == kDtxRpcSid)
        {
            if (rpcNum == 2 && recvBuf && recvSize >= sizeof(uint32_t))
            {
                uint32_t dtxId = 0;
                if (sendBuf && sendSize >= sizeof(uint32_t))
                {
                    (void)readRpcU32(sendBuf, dtxId);
                }

                uint32_t remoteHandle = 0;
                {
                    std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                    auto it = g_dtx_remote_by_id.find(dtxId);
                    if (it != g_dtx_remote_by_id.end())
                    {
                        remoteHandle = it->second;
                    }
                    if (!remoteHandle)
                    {
                        remoteHandle = rpcAllocServerAddr(rdram);
                        if (!remoteHandle)
                        {
                            remoteHandle = rpcAllocPacketAddr(rdram);
                        }
                        if (!remoteHandle)
                        {
                            remoteHandle = kRpcServerPoolBase + ((dtxId & 0xFFu) * kRpcServerStride);
                        }
                        g_dtx_remote_by_id[dtxId] = remoteHandle;
                    }
                }

                (void)writeRpcU32(recvBuf, remoteHandle);
                if (recvSize > sizeof(uint32_t))
                {
                    rpcZeroRdram(rdram, recvBuf + sizeof(uint32_t), recvSize - sizeof(uint32_t));
                }
                handled = true;
                resultPtr = recvBuf;
            }
            else if (rpcNum == 3)
            {
                uint32_t remoteHandle = 0;
                if (sendBuf && sendSize >= sizeof(uint32_t) && readRpcU32(sendBuf, remoteHandle) && remoteHandle)
                {
                    std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                    for (auto it = g_dtx_remote_by_id.begin(); it != g_dtx_remote_by_id.end(); ++it)
                    {
                        if (it->second == remoteHandle)
                        {
                            g_dtx_remote_by_id.erase(it);
                            break;
                        }
                    }
                }
                if (recvBuf && recvSize > 0)
                {
                    rpcZeroRdram(rdram, recvBuf, recvSize);
                }
                handled = true;
                resultPtr = recvBuf;
            }
            else if (rpcNum >= 0x400 && rpcNum < 0x500)
            {
                dtxUrpcFallbackEmulated = true;
                const uint32_t urpcCommand = rpcNum & 0xFFu;
                uint32_t outWords[4] = {1u, 0u, 0u, 0u};
                uint32_t outWordCount = 1u;

                auto readSendWord = [&](uint32_t index, uint32_t &out) -> bool
                {
                    const uint64_t byteOffset = static_cast<uint64_t>(index) * sizeof(uint32_t);
                    if (!sendBuf || sendSize < (byteOffset + sizeof(uint32_t)))
                    {
                        return false;
                    }
                    return readRpcU32(sendBuf + static_cast<uint32_t>(byteOffset), out);
                };

                switch (urpcCommand)
                {
                case 32u: // SJRMT_RBF_CREATE
                case 33u: // SJRMT_MEM_CREATE
                case 34u: // SJRMT_UNI_CREATE
                {
                    uint32_t arg0 = 0;
                    uint32_t arg1 = 0;
                    uint32_t arg2 = 0;
                    (void)readSendWord(0u, arg0);
                    (void)readSendWord(1u, arg1);
                    (void)readSendWord(2u, arg2);

                    uint32_t mode = 0;
                    uint32_t wkAddr = 0;
                    uint32_t wkSize = 0;
                    if (urpcCommand == 34u)
                    {
                        mode = arg0;
                        wkAddr = arg1;
                        wkSize = arg2;
                        dtxUrpcFallbackCreate34 = true;
                    }
                    else if (urpcCommand == 33u)
                    {
                        wkAddr = arg0;
                        wkSize = arg1;
                    }
                    else
                    {
                        wkAddr = arg0;
                        wkSize = (arg1 != 0u) ? arg1 : arg2;
                    }

                    wkSize = dtxNormalizeSjrmtCapacity(wkSize);

                    std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                    const uint32_t handle = dtxAllocUrpcHandleLocked();
                    DtxSjrmtState state{};
                    state.handle = handle;
                    state.mode = mode;
                    state.wkAddr = wkAddr;
                    state.wkSize = wkSize;
                    state.readPos = 0u;
                    state.writePos = 0u;
                    state.roomBytes = wkSize;
                    state.dataBytes = 0u;
                    state.uuid0 = 0x53524D54u; // "SRMT"
                    state.uuid1 = handle;
                    state.uuid2 = wkAddr;
                    state.uuid3 = wkSize;
                    g_dtx_sjrmt_by_handle[handle] = state;

                    outWords[0] = handle ? handle : 1u;
                    outWordCount = 1u;
                    break;
                }
                case 35u: // SJRMT_DESTROY
                {
                    uint32_t handle = 0;
                    (void)readSendWord(0u, handle);
                    std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                    g_dtx_sjrmt_by_handle.erase(handle);
                    outWords[0] = 1u;
                    outWordCount = 1u;
                    break;
                }
                case 36u: // SJRMT_GET_UUID
                {
                    uint32_t handle = 0;
                    (void)readSendWord(0u, handle);
                    std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                    auto it = g_dtx_sjrmt_by_handle.find(handle);
                    if (it != g_dtx_sjrmt_by_handle.end())
                    {
                        outWords[0] = it->second.uuid0;
                        outWords[1] = it->second.uuid1;
                        outWords[2] = it->second.uuid2;
                        outWords[3] = it->second.uuid3;
                    }
                    else
                    {
                        outWords[0] = 0u;
                        outWords[1] = 0u;
                        outWords[2] = 0u;
                        outWords[3] = 0u;
                    }
                    outWordCount = 4u;
                    break;
                }
                case 37u: // SJRMT_RESET
                {
                    uint32_t handle = 0;
                    (void)readSendWord(0u, handle);
                    std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                    auto it = g_dtx_sjrmt_by_handle.find(handle);
                    if (it != g_dtx_sjrmt_by_handle.end())
                    {
                        const uint32_t cap = (it->second.wkSize == 0u) ? 0x4000u : it->second.wkSize;
                        it->second.readPos = 0u;
                        it->second.writePos = 0u;
                        it->second.roomBytes = cap;
                        it->second.dataBytes = 0u;
                    }
                    outWords[0] = 1u;
                    outWordCount = 1u;
                    break;
                }
                case 38u: // SJRMT_GET_CHUNK
                {
                    uint32_t handle = 0;
                    uint32_t streamId = 0;
                    uint32_t nbyte = 0;
                    (void)readSendWord(0u, handle);
                    (void)readSendWord(1u, streamId);
                    (void)readSendWord(2u, nbyte);

                    uint32_t ptr = 0u;
                    uint32_t len = 0u;

                    std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                    auto it = g_dtx_sjrmt_by_handle.find(handle);
                    if (it != g_dtx_sjrmt_by_handle.end())
                    {
                        DtxSjrmtState &state = it->second;
                        const uint32_t cap = (state.wkSize == 0u) ? 0x4000u : state.wkSize;

                        if (streamId == 0u)
                        {
                            len = std::min(nbyte, state.roomBytes);
                            ptr = state.wkAddr + (cap ? (state.writePos % cap) : 0u);
                            if (cap != 0u)
                            {
                                state.writePos = (state.writePos + len) % cap;
                            }
                            state.roomBytes -= len;
                        }
                        else if (streamId == 1u)
                        {
                            len = std::min(nbyte, state.dataBytes);
                            ptr = state.wkAddr + (cap ? (state.readPos % cap) : 0u);
                            if (cap != 0u)
                            {
                                state.readPos = (state.readPos + len) % cap;
                            }
                            state.dataBytes -= len;
                        }
                    }

                    outWords[0] = ptr;
                    outWords[1] = len;
                    outWordCount = 2u;
                    break;
                }
                case 39u: // SJRMT_UNGET_CHUNK
                {
                    uint32_t handle = 0;
                    uint32_t streamId = 0;
                    uint32_t len = 0;
                    (void)readSendWord(0u, handle);
                    (void)readSendWord(1u, streamId);
                    (void)readSendWord(3u, len);

                    std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                    auto it = g_dtx_sjrmt_by_handle.find(handle);
                    if (it != g_dtx_sjrmt_by_handle.end())
                    {
                        DtxSjrmtState &state = it->second;
                        const uint32_t cap = (state.wkSize == 0u) ? 0x4000u : state.wkSize;
                        if (streamId == 0u)
                        {
                            const uint32_t delta = (cap == 0u) ? 0u : (len % cap);
                            if (cap != 0u)
                            {
                                state.writePos = (state.writePos + cap - delta) % cap;
                            }
                            state.roomBytes = std::min(cap, state.roomBytes + len);
                        }
                        else if (streamId == 1u)
                        {
                            const uint32_t delta = (cap == 0u) ? 0u : (len % cap);
                            if (cap != 0u)
                            {
                                state.readPos = (state.readPos + cap - delta) % cap;
                            }
                            state.dataBytes = std::min(cap, state.dataBytes + len);
                        }
                    }

                    outWords[0] = 1u;
                    outWordCount = 1u;
                    break;
                }
                case 40u: // SJRMT_PUT_CHUNK
                {
                    uint32_t handle = 0;
                    uint32_t streamId = 0;
                    uint32_t len = 0;
                    (void)readSendWord(0u, handle);
                    (void)readSendWord(1u, streamId);
                    (void)readSendWord(3u, len);

                    std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                    auto it = g_dtx_sjrmt_by_handle.find(handle);
                    if (it != g_dtx_sjrmt_by_handle.end())
                    {
                        DtxSjrmtState &state = it->second;
                        const uint32_t cap = (state.wkSize == 0u) ? 0x4000u : state.wkSize;
                        if (streamId == 0u)
                        {
                            state.roomBytes = std::min(cap, state.roomBytes + len);
                        }
                        else if (streamId == 1u)
                        {
                            state.dataBytes = std::min(cap, state.dataBytes + len);
                        }
                    }

                    outWords[0] = 1u;
                    outWordCount = 1u;
                    break;
                }
                case 41u: // SJRMT_GET_NUM_DATA
                {
                    uint32_t handle = 0;
                    uint32_t streamId = 0;
                    (void)readSendWord(0u, handle);
                    (void)readSendWord(1u, streamId);

                    std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                    auto it = g_dtx_sjrmt_by_handle.find(handle);
                    if (it != g_dtx_sjrmt_by_handle.end())
                    {
                        outWords[0] = (streamId == 0u) ? it->second.roomBytes : it->second.dataBytes;
                    }
                    else
                    {
                        outWords[0] = 0u;
                    }
                    outWordCount = 1u;
                    break;
                }
                case 42u: // SJRMT_IS_GET_CHUNK
                {
                    uint32_t handle = 0;
                    uint32_t streamId = 0;
                    uint32_t nbyte = 0;
                    (void)readSendWord(0u, handle);
                    (void)readSendWord(1u, streamId);
                    (void)readSendWord(2u, nbyte);

                    uint32_t available = 0u;
                    std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                    auto it = g_dtx_sjrmt_by_handle.find(handle);
                    if (it != g_dtx_sjrmt_by_handle.end())
                    {
                        available = (streamId == 0u) ? it->second.roomBytes : it->second.dataBytes;
                    }
                    outWords[0] = (available >= nbyte) ? 1u : 0u;
                    outWords[1] = available;
                    outWordCount = 2u;
                    break;
                }
                case 43u: // SJRMT_INIT
                case 44u: // SJRMT_FINISH
                {
                    outWords[0] = 1u;
                    outWordCount = 1u;
                    break;
                }
                default:
                {
                    uint32_t urpcRet = 1u;
                    if (sendBuf && sendSize >= sizeof(uint32_t))
                    {
                        (void)readRpcU32(sendBuf, urpcRet);
                    }
                    if (urpcCommand == 0u)
                    {
                        std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
                        urpcRet = dtxAllocUrpcHandleLocked();
                    }
                    if (urpcRet == 0u)
                    {
                        urpcRet = 1u;
                    }
                    outWords[0] = urpcRet;
                    outWordCount = 1u;
                    break;
                }
                }

                if (recvBuf && recvSize > 0u)
                {
                    const uint32_t recvWordCapacity = static_cast<uint32_t>(recvSize / sizeof(uint32_t));
                    const uint32_t wordsToWrite = std::min(outWordCount, recvWordCapacity);
                    for (uint32_t i = 0; i < wordsToWrite; ++i)
                    {
                        (void)writeRpcU32(recvBuf + (i * sizeof(uint32_t)), outWords[i]);
                    }

                    // SJRMT_IsGetChunk callers read rbuf[1] even when nout==1.
                    if (urpcCommand == 42u && outWordCount > 1u)
                    {
                        (void)writeRpcU32(recvBuf + sizeof(uint32_t), outWords[1]);
                    }

                    if (recvSize > (wordsToWrite * sizeof(uint32_t)))
                    {
                        rpcZeroRdram(rdram, recvBuf + (wordsToWrite * sizeof(uint32_t)),
                                     recvSize - (wordsToWrite * sizeof(uint32_t)));
                    }
                }

                handled = true;
                resultPtr = recvBuf;
            }
        }

        if (recvBuf && recvSize > 0)
        {
            if (handled && resultPtr)
            {
                rpcCopyToRdram(rdram, recvBuf, resultPtr, recvSize);
            }
            else if (!handled && sendBuf && sendSize > 0)
            {
                size_t copySize = (sendSize < recvSize) ? sendSize : recvSize;
                rpcCopyToRdram(rdram, recvBuf, sendBuf, copySize);
            }
            else if (!handled)
            {
                rpcZeroRdram(rdram, recvBuf, recvSize);
            }
        }

        if (isDtxUrpc)
        {
            static int dtxUrpcLogCount = 0;
            if (dtxUrpcLogCount < 64)
            {
                uint32_t dtxUrpcRecv0 = 0;
                if (recvBuf && recvSize >= sizeof(uint32_t))
                {
                    (void)readRpcU32(recvBuf, dtxUrpcRecv0);
                }
                std::cout << "[SifCallRpc:DTX] rpcNum=0x" << std::hex << rpcNum
                          << " cmd=0x" << dtxUrpcCommand
                          << " fn=0x" << dtxUrpcFn
                          << " obj=0x" << dtxUrpcObj
                          << " send0=0x" << dtxUrpcSend0
                          << " recv0=0x" << dtxUrpcRecv0
                          << " resultPtr=0x" << resultPtr
                          << " handled=" << std::dec << (handled ? 1 : 0)
                          << " dispatch=" << (dtxUrpcDispatchAttempted ? 1 : 0)
                          << " emu=" << (dtxUrpcFallbackEmulated ? 1 : 0)
                          << " emu34=" << (dtxUrpcFallbackCreate34 ? 1 : 0)
                          << std::endl;
                ++dtxUrpcLogCount;
            }
        }

        if (endFunc)
        {
            rpcInvokeFunction(rdram, ctx, runtime, endFunc, endParam, 0, 0, 0, nullptr);
        }

        static int logCount = 0;
        if (logCount < 10)
        {
            std::cout << "[SifCallRpc] client=0x" << std::hex << clientPtr
                      << " sid=0x" << sid
                      << " rpcNum=0x" << rpcNum
                      << " mode=0x" << mode
                      << " sendBuf=0x" << sendBuf
                      << " recvBuf=0x" << recvBuf
                      << " recvSize=0x" << recvSize
                      << " size=" << std::dec << sendSize << std::endl;
            ++logCount;
        }

        {
            std::lock_guard<std::mutex> lock(g_rpc_mutex);
            g_rpc_clients[clientPtr].busy = false;
        }

        setReturnS32(ctx, 0);
    }

    void SifRegisterRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t sdPtr = getRegU32(ctx, 4);
        uint32_t sid = getRegU32(ctx, 5);
        uint32_t func = getRegU32(ctx, 6);
        uint32_t buf = getRegU32(ctx, 7);
        // stack args: cfunc, cbuf, qd...
        uint32_t sp = getRegU32(ctx, 29);
        uint32_t cfunc = 0;
        uint32_t cbuf = 0;
        uint32_t qd = 0;
        readStackU32(rdram, sp, 0x10, cfunc);
        readStackU32(rdram, sp, 0x14, cbuf);
        readStackU32(rdram, sp, 0x18, qd);

        t_SifRpcServerData *sd = reinterpret_cast<t_SifRpcServerData *>(getMemPtr(rdram, sdPtr));
        if (!sd)
        {
            setReturnS32(ctx, -1);
            return;
        }

        sd->sid = static_cast<int>(sid);
        sd->func = func;
        sd->buf = buf;
        sd->size = 0;
        sd->cfunc = cfunc;
        sd->cbuf = cbuf;
        sd->size2 = 0;
        sd->client = 0;
        sd->pkt_addr = 0;
        sd->rpc_number = 0;
        sd->recvbuf = 0;
        sd->rsize = 0;
        sd->rmode = 0;
        sd->rid = 0;
        sd->base = qd;
        sd->link = 0;
        sd->next = 0;

        if (qd)
        {
            t_SifRpcDataQueue *queue = reinterpret_cast<t_SifRpcDataQueue *>(getMemPtr(rdram, qd));
            if (queue)
            {
                if (!queue->link)
                {
                    queue->link = sdPtr;
                }
                else
                {
                    uint32_t curPtr = queue->link;
                    for (int guard = 0; guard < 1024 && curPtr; ++guard)
                    {
                        t_SifRpcServerData *cur = reinterpret_cast<t_SifRpcServerData *>(getMemPtr(rdram, curPtr));
                        if (!cur)
                            break;
                        if (!cur->link)
                        {
                            cur->link = sdPtr;
                            break;
                        }
                        if (cur->link == sdPtr)
                            break;
                        curPtr = cur->link;
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_rpc_mutex);
            g_rpc_servers[sid] = {sid, sdPtr};
            for (auto &entry : g_rpc_clients)
            {
                if (entry.second.sid == sid)
                {
                    t_SifRpcClientData *cd = reinterpret_cast<t_SifRpcClientData *>(getMemPtr(rdram, entry.first));
                    if (cd)
                    {
                        cd->server = sdPtr;
                        cd->buf = sd->buf;
                        cd->cbuf = sd->cbuf;
                    }
                }
            }
        }

        std::cout << "[SifRegisterRpc] sid=0x" << std::hex << sid << " sd=0x" << sdPtr << std::dec << std::endl;
        setReturnS32(ctx, 0);
    }

    void SifCheckStatRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t clientPtr = getRegU32(ctx, 4);
        std::lock_guard<std::mutex> lock(g_rpc_mutex);
        auto it = g_rpc_clients.find(clientPtr);
        if (it == g_rpc_clients.end())
        {
            setReturnS32(ctx, 0);
            return;
        }
        setReturnS32(ctx, it->second.busy ? 1 : 0);
    }

    void SifSetRpcQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t qdPtr = getRegU32(ctx, 4);
        int threadId = static_cast<int>(getRegU32(ctx, 5));

        t_SifRpcDataQueue *qd = reinterpret_cast<t_SifRpcDataQueue *>(getMemPtr(rdram, qdPtr));
        if (!qd)
        {
            setReturnS32(ctx, -1);
            return;
        }

        qd->thread_id = threadId;
        qd->active = 0;
        qd->link = 0;
        qd->start = 0;
        qd->end = 0;
        qd->next = 0;

        {
            std::lock_guard<std::mutex> lock(g_rpc_mutex);
            if (!g_rpc_active_queue)
            {
                g_rpc_active_queue = qdPtr;
            }
            else
            {
                uint32_t curPtr = g_rpc_active_queue;
                for (int guard = 0; guard < 1024 && curPtr; ++guard)
                {
                    if (curPtr == qdPtr)
                        break;
                    t_SifRpcDataQueue *cur = reinterpret_cast<t_SifRpcDataQueue *>(getMemPtr(rdram, curPtr));
                    if (!cur)
                        break;
                    if (!cur->next)
                    {
                        cur->next = qdPtr;
                        break;
                    }
                    curPtr = cur->next;
                }
            }
        }

        setReturnS32(ctx, 0);
    }

    void SifRemoveRpcQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t qdPtr = getRegU32(ctx, 4);
        if (!qdPtr)
        {
            setReturnU32(ctx, 0);
            return;
        }

        std::lock_guard<std::mutex> lock(g_rpc_mutex);
        if (!g_rpc_active_queue)
        {
            setReturnU32(ctx, 0);
            return;
        }

        if (g_rpc_active_queue == qdPtr)
        {
            t_SifRpcDataQueue *qd = reinterpret_cast<t_SifRpcDataQueue *>(getMemPtr(rdram, qdPtr));
            g_rpc_active_queue = qd ? qd->next : 0;
            setReturnU32(ctx, qdPtr);
            return;
        }

        uint32_t curPtr = g_rpc_active_queue;
        for (int guard = 0; guard < 1024 && curPtr; ++guard)
        {
            t_SifRpcDataQueue *cur = reinterpret_cast<t_SifRpcDataQueue *>(getMemPtr(rdram, curPtr));
            if (!cur)
                break;
            if (cur->next == qdPtr)
            {
                t_SifRpcDataQueue *rem = reinterpret_cast<t_SifRpcDataQueue *>(getMemPtr(rdram, qdPtr));
                cur->next = rem ? rem->next : 0;
                setReturnU32(ctx, qdPtr);
                return;
            }
            curPtr = cur->next;
        }

        setReturnU32(ctx, 0);
    }

    void SifRemoveRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t sdPtr = getRegU32(ctx, 4);
        uint32_t qdPtr = getRegU32(ctx, 5);

        t_SifRpcDataQueue *qd = reinterpret_cast<t_SifRpcDataQueue *>(getMemPtr(rdram, qdPtr));
        if (!qd || !sdPtr)
        {
            setReturnU32(ctx, 0);
            return;
        }

        if (qd->link == sdPtr)
        {
            t_SifRpcServerData *sd = reinterpret_cast<t_SifRpcServerData *>(getMemPtr(rdram, sdPtr));
            qd->link = sd ? sd->link : 0;
            if (sd)
                sd->link = 0;
            setReturnU32(ctx, sdPtr);
            return;
        }

        uint32_t curPtr = qd->link;
        for (int guard = 0; guard < 1024 && curPtr; ++guard)
        {
            t_SifRpcServerData *cur = reinterpret_cast<t_SifRpcServerData *>(getMemPtr(rdram, curPtr));
            if (!cur)
                break;
            if (cur->link == sdPtr)
            {
                t_SifRpcServerData *sd = reinterpret_cast<t_SifRpcServerData *>(getMemPtr(rdram, sdPtr));
                cur->link = sd ? sd->link : 0;
                if (sd)
                    sd->link = 0;
                setReturnU32(ctx, sdPtr);
                return;
            }
            curPtr = cur->link;
        }

        setReturnU32(ctx, 0);
    }

    void sceSifCallRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        SifCallRpc(rdram, ctx, runtime);
    }

    void sceSifSendCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t cid = getRegU32(ctx, 4);
        uint32_t packetAddr = getRegU32(ctx, 5);
        uint32_t packetSize = getRegU32(ctx, 6);
        uint32_t srcExtra = getRegU32(ctx, 7);

        uint32_t sp = getRegU32(ctx, 29);
        uint32_t destExtra = 0;
        uint32_t sizeExtra = 0;
        readStackU32(rdram, sp, 0x10, destExtra);
        readStackU32(rdram, sp, 0x14, sizeExtra);

        if (sizeExtra > 0 && srcExtra && destExtra)
        {
            rpcCopyToRdram(rdram, destExtra, srcExtra, sizeExtra);
        }

        static int logCount = 0;
        if (logCount < 5)
        {
            std::cout << "[sceSifSendCmd] cid=0x" << std::hex << cid
                      << " packet=0x" << packetAddr
                      << " psize=0x" << packetSize
                      << " extra=0x" << destExtra << std::dec << std::endl;
            ++logCount;
        }

        // Return non-zero on success.
        setReturnS32(ctx, 1);
    }

    void _sceRpcGetPacket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t queuePtr = getRegU32(ctx, 4);
        setReturnS32(ctx, static_cast<int32_t>(queuePtr));
    }

    void fioOpen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4); // $a0
        int flags = (int)getRegU32(ctx, 5);    // $a1 (PS2 FIO flags)

        const char *ps2Path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        if (!ps2Path)
        {
            std::cerr << "fioOpen error: Invalid path address" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        std::string hostPath = translatePs2Path(ps2Path);
        if (hostPath.empty())
        {
            std::cerr << "fioOpen error: Failed to translate path '" << ps2Path << "'" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        const char *mode = translateFioMode(flags);
        std::cout << "fioOpen: '" << hostPath << "' flags=0x" << std::hex << flags << std::dec << " mode='" << mode << "'" << std::endl;

        FILE *fp = ::fopen(hostPath.c_str(), mode);
        if (!fp)
        {
            std::cerr << "fioOpen error: fopen failed for '" << hostPath << "': " << strerror(errno) << std::endl;
            setReturnS32(ctx, -1); // e.g., -ENOENT, -EACCES
            return;
        }

        int ps2Fd = allocatePs2Fd(fp);
        if (ps2Fd < 0)
        {
            std::cerr << "fioOpen error: Failed to allocate PS2 file descriptor" << std::endl;
            ::fclose(fp);
            setReturnS32(ctx, -1); // e.g., -EMFILE
            return;
        }

        // returns the PS2 file descriptor
        setReturnS32(ctx, ps2Fd);
    }

    void fioClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int ps2Fd = (int)getRegU32(ctx, 4); // $a0
        std::cout << "fioClose: fd=" << ps2Fd << std::endl;

        FILE *fp = getHostFile(ps2Fd);
        if (!fp)
        {
            std::cerr << "fioClose warning: Invalid PS2 file descriptor " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // e.g., -EBADF
            return;
        }

        int ret = ::fclose(fp);
        releasePs2Fd(ps2Fd);

        // returns 0 on success, -1 on error
        setReturnS32(ctx, ret == 0 ? 0 : -1);
    }

    void fioRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int ps2Fd = (int)getRegU32(ctx, 4);   // $a0
        uint32_t bufAddr = getRegU32(ctx, 5); // $a1
        size_t size = getRegU32(ctx, 6);      // $a2

        uint8_t *hostBuf = getMemPtr(rdram, bufAddr);
        FILE *fp = getHostFile(ps2Fd);

        if (!hostBuf)
        {
            std::cerr << "fioRead error: Invalid buffer address for fd " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EFAULT
            return;
        }
        if (!fp)
        {
            std::cerr << "fioRead error: Invalid file descriptor " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EBADF
            return;
        }
        if (size == 0)
        {
            setReturnS32(ctx, 0); // Read 0 bytes
            return;
        }

        size_t bytesRead = 0;
        {
            std::lock_guard<std::mutex> lock(g_sys_fd_mutex);
            bytesRead = fread(hostBuf, 1, size, fp);
        }

        if (bytesRead < size && ferror(fp))
        {
            std::cerr << "fioRead error: fread failed for fd " << ps2Fd << ": " << strerror(errno) << std::endl;
            clearerr(fp);
            setReturnS32(ctx, -1); // -EIO or other appropriate error
            return;
        }

        // returns number of bytes read (can be 0 for EOF)
        setReturnS32(ctx, (int32_t)bytesRead);
    }

    void fioWrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int ps2Fd = (int)getRegU32(ctx, 4);   // $a0
        uint32_t bufAddr = getRegU32(ctx, 5); // $a1
        size_t size = getRegU32(ctx, 6);      // $a2

        const uint8_t *hostBuf = getConstMemPtr(rdram, bufAddr);
        FILE *fp = getHostFile(ps2Fd);

        if (!hostBuf)
        {
            std::cerr << "fioWrite error: Invalid buffer address for fd " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EFAULT
            return;
        }
        if (!fp)
        {
            std::cerr << "fioWrite error: Invalid file descriptor " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EBADF
            return;
        }
        if (size == 0)
        {
            setReturnS32(ctx, 0); // Wrote 0 bytes
            return;
        }

        size_t bytesWritten = ::fwrite(hostBuf, 1, size, fp);

        if (bytesWritten < size)
        {
            if (ferror(fp))
            {
                std::cerr << "fioWrite error: fwrite failed for fd " << ps2Fd << ": " << strerror(errno) << std::endl;
                clearerr(fp);
                setReturnS32(ctx, -1); // -EIO, -ENOSPC etc.
            }
            else
            {
                // Partial write without error? Possible but idk.
                setReturnS32(ctx, (int32_t)bytesWritten);
            }
            return;
        }

        // returns number of bytes written
        setReturnS32(ctx, (int32_t)bytesWritten);
    }

    void fioLseek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int ps2Fd = (int)getRegU32(ctx, 4);  // $a0
        int32_t offset = getRegU32(ctx, 5);  // $a1 (PS2 seems to use 32-bit offset here commonly)
        int whence = (int)getRegU32(ctx, 6); // $a2 (PS2 FIO_SEEK constants)

        FILE *fp = getHostFile(ps2Fd);
        if (!fp)
        {
            std::cerr << "fioLseek error: Invalid file descriptor " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EBADF
            return;
        }

        int hostWhence;
        switch (whence)
        {
        case PS2_FIO_SEEK_SET:
            hostWhence = SEEK_SET;
            break;
        case PS2_FIO_SEEK_CUR:
            hostWhence = SEEK_CUR;
            break;
        case PS2_FIO_SEEK_END:
            hostWhence = SEEK_END;
            break;
        default:
            std::cerr << "fioLseek error: Invalid whence value " << whence << " for fd " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EINVAL
            return;
        }

        if (::fseek(fp, static_cast<long>(offset), hostWhence) != 0)
        {
            std::cerr << "fioLseek error: fseek failed for fd " << ps2Fd << ": " << strerror(errno) << std::endl;
            setReturnS32(ctx, -1); // Return error code
            return;
        }

        long newPos = ::ftell(fp);
        if (newPos < 0)
        {
            std::cerr << "fioLseek error: ftell failed after fseek for fd " << ps2Fd << ": " << strerror(errno) << std::endl;
            setReturnS32(ctx, -1);
        }
        else
        {
            if (newPos > 0xFFFFFFFFL)
            {
                std::cerr << "fioLseek warning: New position exceeds 32-bit for fd " << ps2Fd << std::endl;
                setReturnS32(ctx, -1);
            }
            else
            {
                setReturnS32(ctx, (int32_t)newPos);
            }
        }
    }

    void fioMkdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4); // $a0
        // int mode = (int)getRegU32(ctx, 5);

        const char *ps2Path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        if (!ps2Path)
        {
            std::cerr << "fioMkdir error: Invalid path address" << std::endl;
            setReturnS32(ctx, -1); // -EFAULT
            return;
        }
        std::string hostPath = translatePs2Path(ps2Path);
        if (hostPath.empty())
        {
            std::cerr << "fioMkdir error: Failed to translate path '" << ps2Path << "'" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

#ifdef _WIN32
        int ret = -1;
#else
        int ret = ::mkdir(hostPath.c_str(), 0775);
#endif

        if (ret != 0)
        {
            std::cerr << "fioMkdir error: mkdir failed for '" << hostPath << "': " << strerror(errno) << std::endl;
            setReturnS32(ctx, -1); // errno
        }
        else
        {
            setReturnS32(ctx, 0); // Success
        }
    }

    void fioChdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4); // $a0
        const char *ps2Path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        if (!ps2Path)
        {
            std::cerr << "fioChdir error: Invalid path address" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        std::string hostPath = translatePs2Path(ps2Path);
        if (hostPath.empty())
        {
            std::cerr << "fioChdir error: Failed to translate path '" << ps2Path << "'" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        std::cerr << "fioChdir: Attempting host chdir to '" << hostPath << "' (Stub - Check side effects)" << std::endl;

#ifdef _WIN32
        int ret = -1;
#else
        int ret = ::chdir(hostPath.c_str());
#endif

        if (ret != 0)
        {
            std::cerr << "fioChdir error: chdir failed for '" << hostPath << "': " << strerror(errno) << std::endl;
            setReturnS32(ctx, -1);
        }
        else
        {
            setReturnS32(ctx, 0); // Success
        }
    }

    void fioRmdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4); // $a0
        const char *ps2Path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        if (!ps2Path)
        {
            std::cerr << "fioRmdir error: Invalid path address" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }
        std::string hostPath = translatePs2Path(ps2Path);
        if (hostPath.empty())
        {
            std::cerr << "fioRmdir error: Failed to translate path '" << ps2Path << "'" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

#ifdef _WIN32
        int ret = -1;
#else
        int ret = ::rmdir(hostPath.c_str());
#endif

        if (ret != 0)
        {
            std::cerr << "fioRmdir error: rmdir failed for '" << hostPath << "': " << strerror(errno) << std::endl;
            setReturnS32(ctx, -1);
        }
        else
        {
            setReturnS32(ctx, 0); // Success
        }
    }

    void fioGetstat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // we wont implement this for now.
        uint32_t pathAddr = getRegU32(ctx, 4);    // $a0
        uint32_t statBufAddr = getRegU32(ctx, 5); // $a1

        const char *ps2Path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        uint8_t *ps2StatBuf = getMemPtr(rdram, statBufAddr);

        if (!ps2Path)
        {
            std::cerr << "fioGetstat error: Invalid path addr" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }
        if (!ps2StatBuf)
        {
            std::cerr << "fioGetstat error: Invalid buffer addr" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        std::string hostPath = translatePs2Path(ps2Path);
        if (hostPath.empty())
        {
            std::cerr << "fioGetstat error: Bad path translate" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        setReturnS32(ctx, -1);
    }

    void fioRemove(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4); // $a0
        const char *ps2Path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        if (!ps2Path)
        {
            std::cerr << "fioRemove error: Invalid path" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        std::string hostPath = translatePs2Path(ps2Path);
        if (hostPath.empty())
        {
            std::cerr << "fioRemove error: Path translate fail" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

#ifdef _WIN32
        int ret = -1;
#else
        int ret = ::unlink(hostPath.c_str());
#endif

        if (ret != 0)
        {
            std::cerr << "fioRemove error: unlink failed for '" << hostPath << "': " << strerror(errno) << std::endl;
            setReturnS32(ctx, -1);
        }
        else
        {
            setReturnS32(ctx, 0); // Success
        }
    }

    void GsSetCrt(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int interlaced = getRegU32(ctx, 4); // $a0 - 0=non-interlaced, 1=interlaced
        int videoMode = getRegU32(ctx, 5);  // $a1 - 0=NTSC, 1=PAL, 2=VESA, 3=HiVision
        int frameMode = getRegU32(ctx, 6);  // $a2 - 0=field, 1=frame

        std::cout << "PS2 GsSetCrt: interlaced=" << interlaced
                  << ", videoMode=" << videoMode
                  << ", frameMode=" << frameMode << std::endl;
    }

    void GsGetIMR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint64_t imr = 0;
        if (runtime)
        {
            imr = runtime->memory().gs().imr;
        }

        std::cout << "PS2 GsGetIMR: Returning IMR=0x" << std::hex << imr << std::dec << std::endl;

        setReturnU64(ctx, imr); // Return in $v0/$v1
    }

    void GsPutIMR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint64_t newImr = getRegU32(ctx, 4) | ((uint64_t)getRegU32(ctx, 5) << 32); // $a0 = lower 32 bits, $a1 = upper 32 bits
        uint64_t oldImr = 0;
        if (runtime)
        {
            oldImr = runtime->memory().gs().imr;
            runtime->memory().gs().imr = newImr;
        }
        std::cout << "PS2 GsPutIMR: Setting IMR=0x" << std::hex << newImr << std::dec << std::endl;
        setReturnU64(ctx, oldImr);
    }

    void GsSetVideoMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int mode = getRegU32(ctx, 4); // $a0 - video mode (various flags)

        std::cout << "PS2 GsSetVideoMode: mode=0x" << std::hex << mode << std::dec << std::endl;

        // Do nothing for now.
    }

    void GetOsdConfigParam(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t paramAddr = getRegU32(ctx, 4); // $a0 - pointer to parameter structure

        if (!getMemPtr(rdram, paramAddr))
        {
            std::cerr << "PS2 GetOsdConfigParam error: Invalid parameter address: 0x"
                      << std::hex << paramAddr << std::dec << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        uint32_t *param = reinterpret_cast<uint32_t *>(getMemPtr(rdram, paramAddr));

        ensureOsdConfigInitialized();
        uint32_t raw;
        {
            std::lock_guard<std::mutex> lock(g_osd_mutex);
            raw = g_osd_config_raw;
        }

        *param = raw;

        setReturnS32(ctx, 0);
    }

    void SetOsdConfigParam(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t paramAddr = getRegU32(ctx, 4); // $a0 - pointer to parameter structure

        if (!getConstMemPtr(rdram, paramAddr))
        {
            std::cerr << "PS2 SetOsdConfigParam error: Invalid parameter address: 0x"
                      << std::hex << paramAddr << std::dec << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        const uint32_t *param = reinterpret_cast<const uint32_t *>(getConstMemPtr(rdram, paramAddr));
        uint32_t raw = param ? *param : 0;
        raw = sanitizeOsdConfigRaw(raw);
        {
            std::lock_guard<std::mutex> lock(g_osd_mutex);
            g_osd_config_raw = raw;
            g_osd_config_initialized = true;
        }

        setReturnS32(ctx, 0);
    }

    void GetRomName(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t bufAddr = getRegU32(ctx, 4); // $a0
        size_t bufSize = getRegU32(ctx, 5);   // $a1
        char *hostBuf = reinterpret_cast<char *>(getMemPtr(rdram, bufAddr));
        const char *romName = "ROMVER 0100";

        if (!hostBuf)
        {
            std::cerr << "GetRomName error: Invalid buffer address" << std::endl;
            setReturnS32(ctx, -1); // Error
            return;
        }
        if (bufSize == 0)
        {
            setReturnS32(ctx, 0);
            return;
        }

        strncpy(hostBuf, romName, bufSize - 1);
        hostBuf[bufSize - 1] = '\0';

        // returns the length of the string (excluding null?) or error
        setReturnS32(ctx, (int32_t)strlen(hostBuf));
    }

    void SifLoadElfPart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t pathAddr = getRegU32(ctx, 4);    // $a0 - path
        const uint32_t secNameAddr = getRegU32(ctx, 5); // $a1 - section name ("all" typically)
        const uint32_t execDataAddr = getRegU32(ctx, 6); // $a2 - t_ExecData*

        std::string secName = readGuestCStringBounded(rdram, secNameAddr, kLoadfileArgMaxBytes);
        if (secName.empty())
        {
            secName = "all";
        }

        const int32_t ret = runSifLoadElfPart(rdram, ctx, runtime, pathAddr, secName, execDataAddr);
        setReturnS32(ctx, ret);
    }

    void sceSifLoadElf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t pathAddr = getRegU32(ctx, 4);     // $a0 - path
        const uint32_t execDataAddr = getRegU32(ctx, 5); // $a1 - t_ExecData*
        const int32_t ret = runSifLoadElfPart(rdram, ctx, runtime, pathAddr, "all", execDataAddr);
        setReturnS32(ctx, ret);
    }

    void sceSifLoadElfPart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        SifLoadElfPart(rdram, ctx, runtime);
    }

    void sceSifLoadModule(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // Use the same tracker as SifLoadModule so both APIs return the same module IDs.
        SifLoadModule(rdram, ctx, runtime);
    }

    void sceSifLoadModuleBuffer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t bufferAddr = getRegU32(ctx, 4); // $a0
        if (!rdram || bufferAddr == 0u)
        {
            setReturnS32(ctx, -1);
            return;
        }

        // Match buffer-based module loads to stable synthetic tags so module ID lookup remains deterministic.
        const std::string moduleTag = makeSifModuleBufferTag(rdram, bufferAddr);
        const int32_t moduleId = trackSifModuleLoad(moduleTag);
        if (moduleId <= 0)
        {
            setReturnS32(ctx, -1);
            return;
        }

        uint32_t refs = 0;
        {
            std::lock_guard<std::mutex> lock(g_sif_module_mutex);
            auto it = g_sif_modules_by_id.find(moduleId);
            if (it != g_sif_modules_by_id.end())
            {
                refs = it->second.refCount;
            }
        }
        logSifModuleAction("load-buffer", moduleId, moduleTag, refs);
        setReturnS32(ctx, moduleId);
    }

    void TODO(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t encodedSyscallId)
    {
        const uint32_t v0 = getRegU32(ctx, 2);
        const uint32_t v1 = getRegU32(ctx, 3);
        const uint32_t caller_ra = getRegU32(ctx, 31);
        uint32_t syscallId = encodedSyscallId;
        if (syscallId == 0u)
        {
            syscallId = (v0 != 0u) ? v0 : v1;
        }

        std::cerr << "Warning: Unimplemented PS2 syscall called. PC=0x" << std::hex << ctx->pc
                  << ", RA=0x" << caller_ra
                  << ", Encoded=0x" << encodedSyscallId
                  << ", v0=0x" << v0
                  << ", v1=0x" << v1
                  << ", Chosen=0x" << syscallId
                  << std::dec << std::endl;

        std::cerr << "  Args: $a0=0x" << std::hex << getRegU32(ctx, 4)
                  << ", $a1=0x" << getRegU32(ctx, 5)
                  << ", $a2=0x" << getRegU32(ctx, 6)
                  << ", $a3=0x" << getRegU32(ctx, 7) << std::dec << std::endl;

        // Common syscalls:
        // 0x04: Exit
        // 0x06: LoadExecPS2
        // 0x07: ExecPS2
        if (syscallId == 0x04u)
        {
            std::cerr << "  -> Syscall is Exit(), calling ExitThread stub." << std::endl;
            ExitThread(rdram, ctx, runtime);
            return;
        }

        static std::mutex s_unknownMutex;
        static std::unordered_map<uint32_t, uint64_t> s_unknownCounts;
        {
            std::lock_guard<std::mutex> lock(s_unknownMutex);
            const uint64_t count = ++s_unknownCounts[syscallId];
            if (count == 1 || (count % 5000u) == 0u)
            {
                std::cerr << "  -> Unknown syscallId=0x" << std::hex << syscallId
                          << " hits=" << std::dec << count << std::endl;
            }
        }

        // Bootstrap default: avoid hard-failing loops that probe syscall availability.
        setReturnS32(ctx, 0);
    }

    // 0x3C SetupThread: returns stack pointer (stack + stack_size)
    // args: $a0 = stack base, $a1 = stack size, $a2 = gp, $a3 = entry point
    void SetupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t stackBase = getRegU32(ctx, 4);
        uint32_t stackSize = getRegU32(ctx, 5);
        uint32_t sp = stackBase + stackSize;
        setReturnS32(ctx, sp);
    }

    // 0x3D SetupHeap: returns heap base/start pointer
    void SetupHeap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t heapBase = getRegU32(ctx, 4); // $a0
        const uint32_t heapSize = getRegU32(ctx, 5); // $a1 (optional size)

        if (runtime)
        {
            uint32_t heapLimit = PS2_RAM_SIZE;
            if (heapSize != 0u && heapBase < PS2_RAM_SIZE)
            {
                const uint64_t candidateLimit = static_cast<uint64_t>(heapBase) + static_cast<uint64_t>(heapSize);
                heapLimit = static_cast<uint32_t>(std::min<uint64_t>(candidateLimit, PS2_RAM_SIZE));
            }
            runtime->configureGuestHeap(heapBase, heapLimit);
            setReturnU32(ctx, runtime->guestHeapBase());
            return;
        }

        setReturnU32(ctx, heapBase);
    }

    // 0x3E EndOfHeap: commonly returns current heap end; keep it stable for now.
    void EndOfHeap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (runtime)
        {
            setReturnU32(ctx, runtime->guestHeapEnd());
            return;
        }

        setReturnU32(ctx, getRegU32(ctx, 4));
    }

    // 0x5A QueryBootMode (stub): return 0 for now
    void QueryBootMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t mode = getRegU32(ctx, 4);
        ensureBootModeTable(rdram);
        uint32_t addr = 0;
        {
            std::lock_guard<std::mutex> lock(g_bootmode_mutex);
            auto it = g_bootmode_addresses.find(static_cast<uint8_t>(mode));
            if (it != g_bootmode_addresses.end())
                addr = it->second;
        }
        setReturnU32(ctx, addr);
    }

    // 0x5B GetThreadTLS (stub): return 0
    void GetThreadTLS(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        auto info = ensureCurrentThreadInfo(ctx);
        if (!info)
        {
            setReturnU32(ctx, 0);
            return;
        }

        if (info->tlsBase == 0)
        {
            info->tlsBase = allocTlsAddr(rdram);
        }

        setReturnU32(ctx, info->tlsBase);
    }

    // 0x74 RegisterExitHandler (stub): return 0
    void RegisterExitHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t func = getRegU32(ctx, 4);
        uint32_t arg = getRegU32(ctx, 5);
        if (func == 0)
        {
            setReturnS32(ctx, -1);
            return;
        }

        int tid = g_currentThreadId;
        {
            std::lock_guard<std::mutex> lock(g_exit_handler_mutex);
            g_exit_handlers[tid].push_back({func, arg});
        }

        setReturnS32(ctx, 0);
    }
}
