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
    bool ownsStack = false;
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
constexpr int KE_ILLEGAL_PRIORITY = -403;
constexpr int KE_ILLEGAL_MODE = -405;
constexpr int KE_ILLEGAL_THID = -406;
constexpr int KE_UNKNOWN_THID = -407;
constexpr int KE_UNKNOWN_SEMID = -408;
constexpr int KE_UNKNOWN_EVFID = -409;
constexpr int KE_DORMANT = -413;
constexpr int KE_NOT_DORMANT = -414;
constexpr int KE_NOT_SUSPEND = -415;
constexpr int KE_NOT_WAIT = -416;
constexpr int KE_RELEASE_WAIT = -418;
constexpr int KE_SEMA_ZERO = -419;
constexpr int KE_SEMA_OVF = -420;
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
    bool deleted = false;
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
static std::unordered_map<int, std::thread> g_hostThreads;
static std::mutex g_host_thread_mutex;

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
static std::mutex g_fd_mutex;

static void registerHostThread(int tid, std::thread worker)
{
    std::thread stale;
    {
        std::lock_guard<std::mutex> lock(g_host_thread_mutex);
        auto it = g_hostThreads.find(tid);
        if (it != g_hostThreads.end())
        {
            stale = std::move(it->second);
            g_hostThreads.erase(it);
        }
        g_hostThreads.emplace(tid, std::move(worker));
    }

    if (stale.joinable())
    {
        if (stale.get_id() == std::this_thread::get_id())
        {
            stale.detach();
        }
        else
        {
            stale.join();
        }
    }
}

static void joinHostThreadById(int tid)
{
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(g_host_thread_mutex);
        auto it = g_hostThreads.find(tid);
        if (it != g_hostThreads.end())
        {
            worker = std::move(it->second);
            g_hostThreads.erase(it);
        }
    }

    if (!worker.joinable())
    {
        return;
    }

    if (worker.get_id() == std::this_thread::get_id())
    {
        worker.detach();
    }
    else
    {
        worker.join();
    }
}

static void joinAllHostThreads()
{
    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(g_host_thread_mutex);
        workers.reserve(g_hostThreads.size());
        const std::thread::id selfId = std::this_thread::get_id();
        for (auto it = g_hostThreads.begin(); it != g_hostThreads.end();)
        {
            std::thread &worker = it->second;
            if (worker.joinable() && worker.get_id() == selfId)
            {
                ++it;
                continue;
            }

            workers.push_back(std::move(worker));
            it = g_hostThreads.erase(it);
        }
    }

    for (auto &worker : workers)
    {
        if (!worker.joinable())
        {
            continue;
        }
        worker.join();
    }
}

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
