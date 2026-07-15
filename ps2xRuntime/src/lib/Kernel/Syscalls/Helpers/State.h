#pragma once

#include <cstddef>
#include <cstdint>

inline std::unordered_map<int, FILE *> g_fileDescriptors;
inline int g_nextFd = 3; // Start after stdin, stdout, stderr

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
    std::atomic<uint32_t> currentPc{0};

    std::mutex m;
    std::atomic<bool> forceRelease{false};
    std::atomic<bool> terminated{false};
    // Set true when StartThread mints this thread's g_activeThreads token
    // (fetch_add). Cleared by exactly ONE consumer via exchange(true->false):
    // on_fiber_exit for a normal exit, ExitDeleteThread when it removes its own
    // g_threads entry, notifyRuntimeStop when it reaps residual guest threads,
    // or StartThread's own create_fiber failure path. Whoever wins the exchange
    // performs the single matching fetch_sub: one token per started thread,
    // exactly one decrement.
    std::atomic<bool> activeCounted{false};
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
constexpr int KE_NO_MEMORY = -400;
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
    // Wait list of blocked guest threads. Each entry is {tid, generation token}
    // where the token was captured via ps2sched::current_fiber_token() at push
    // time. Protected by m; never hold m across a scheduling yield.
    std::vector<std::pair<int, ps2sched::FiberToken>> waitList;
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
    // See SemaInfo::waitList.
    std::vector<std::pair<int, ps2sched::FiberToken>> waitList;
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

inline std::unordered_map<int, std::shared_ptr<ThreadInfo>> g_threads;
inline int g_nextThreadId = 2; // Reserve 1 for the main thread
extern thread_local int g_currentThreadId;
inline std::mutex g_thread_map_mutex;

inline std::unordered_map<int, std::shared_ptr<SemaInfo>> g_semas;
inline int g_nextSemaId = 1;
inline std::mutex g_sema_map_mutex;
inline std::unordered_map<int, std::shared_ptr<EventFlagInfo>> g_eventFlags;
inline int g_nextEventFlagId = 1;
inline std::mutex g_event_flag_map_mutex;
inline std::unordered_map<int, std::shared_ptr<AlarmInfo>> g_alarms;
inline int g_nextAlarmId = 1;
inline std::mutex g_alarm_mutex;
inline std::condition_variable g_alarm_cv;
// Stop mechanism. g_alarm_thread is joinable so stopAlarmWorker() can join it
// before rdram/runtime are destroyed.
inline std::thread g_alarm_thread;
inline std::atomic<bool> g_alarm_stop_flag{false};
// Plain resettable flag (not once-only) so stopAlarmWorker() can clear it and
// allow ensureAlarmWorkerRunning() to restart the thread in a subsequent
// scheduler cycle (e.g. repeated init/shutdown in the test suite).
// Protected by g_alarm_mutex.
inline bool g_alarm_worker_running{false};
inline std::atomic<int> g_activeThreads{0};

// Mint/consume pair for ThreadInfo::activeCounted (see the field comment
// above). MINT bumps g_activeThreads FIRST, then publishes the token with
// release, so a consumer observing activeCounted==true is guaranteed to also
// see the matching +1. CONSUME is the sole arbiter for a given token: the
// exchange(false) only the winner of the true->false transition performs the
// matching fetch_sub, so concurrent consumers (e.g. on_fiber_exit racing
// notifyRuntimeStop over the same shared_ptr) can never double-decrement.
static inline void mintActiveToken(const std::shared_ptr<ThreadInfo> &info)
{
    g_activeThreads.fetch_add(1, std::memory_order_relaxed);
    info->activeCounted.store(true, std::memory_order_release);
}
static inline void consumeActiveToken(const std::shared_ptr<ThreadInfo> &info)
{
    if (info && info->activeCounted.exchange(false, std::memory_order_acq_rel))
        g_activeThreads.fetch_sub(1, std::memory_order_release);
}

inline std::mutex g_fd_mutex;

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

struct SifRpcDebugEvent
{
    uint64_t seq = 0;
    const char *op = "";
    uint32_t pc = 0;
    uint32_t ra = 0;
    uint32_t threadId = 0;
    uint32_t sid = 0;
    uint32_t rpcNum = 0;
    uint32_t clientPtr = 0;
    uint32_t serverPtr = 0;
    uint32_t sendBuf = 0;
    uint32_t sendSize = 0;
    uint32_t recvBuf = 0;
    uint32_t recvSize = 0;
    uint32_t resultPtr = 0;
    uint32_t mode = 0;
    uint32_t endFunc = 0;
    uint32_t endParam = 0;
    uint32_t semaId = 0;
    uint32_t flags = 0;
    uint32_t sendPreviewSize = 0;
    uint32_t recvPreviewSize = 0;
    uint8_t sendPreview[16]{};
    uint8_t recvPreview[16]{};
    int32_t result = 0;
};

static constexpr size_t kSifRpcDebugHistoryCount = 256u;
static constexpr size_t kSifRpcDebugPreviewBytes = 16u;
static constexpr uint32_t kSifRpcDebugFlagNowait = 1u << 0;
static constexpr uint32_t kSifRpcDebugFlagHandledByHle = 1u << 1;
static constexpr uint32_t kSifRpcDebugFlagCallback = 1u << 2;
static constexpr uint32_t kSifRpcDebugFlagMissingClient = 1u << 3;
static constexpr uint32_t kSifRpcDebugFlagServerDispatch = 1u << 4;
static constexpr uint32_t kSifRpcDebugFlagUnhandled = 1u << 6;
static constexpr uint32_t kSifRpcDebugFlagFallbackCopy = 1u << 7;
static constexpr uint32_t kSifRpcDebugFlagFallbackZero = 1u << 8;

inline std::unordered_map<uint32_t, RpcServerState> g_rpc_servers;
inline std::unordered_map<uint32_t, RpcClientState> g_rpc_clients;
inline SifRpcDebugEvent g_sif_rpc_debug_history[kSifRpcDebugHistoryCount]{};
inline uint64_t g_sif_rpc_debug_next_seq = 0;
inline std::mutex g_rpc_mutex;
inline std::recursive_mutex g_sif_call_rpc_mutex;
inline bool g_rpc_initialized = false;
inline uint32_t g_rpc_next_id = 1;
inline uint32_t g_rpc_packet_index = 0;
inline uint32_t g_rpc_server_index = 0;
inline uint32_t g_rpc_active_queue = 0;
struct ExitHandlerEntry
{
    uint32_t func = 0;
    uint32_t arg = 0;
};

inline std::mutex g_exit_handler_mutex;
inline std::unordered_map<int, std::vector<ExitHandlerEntry>> g_exit_handlers;

inline std::mutex g_bootmode_mutex;
inline bool g_bootmode_initialized = false;
inline uint32_t g_bootmode_pool_offset = 0;
inline std::unordered_map<uint8_t, uint32_t> g_bootmode_addresses;

inline std::mutex g_syscall_override_mutex;
inline std::unordered_map<uint32_t, uint32_t> g_syscall_overrides;
inline std::unordered_set<uint32_t> g_syscall_mirror_addrs;

static constexpr uint32_t kGuestSyscallTableGuestBase = 0x80011F80u;
static constexpr uint32_t kGuestSyscallTablePhysBase = kGuestSyscallTableGuestBase & 0x1FFFFFFFu;
static constexpr uint32_t kGuestSyscallMirrorLimit = 0x00080000u;
static constexpr uint32_t kGuestSyscallTableProbeBase = 0x000002F0u;

inline std::mutex g_tls_mutex;
inline uint32_t g_tls_index = 0;

inline std::mutex g_osd_mutex;
inline bool g_osd_config_initialized = false;
inline uint32_t g_osd_config_raw = 0;
inline uint32_t g_osd_config2_raw = 0;

inline std::mutex g_ps2_path_mutex;
inline bool g_ps2_paths_initialized = false;
inline std::filesystem::path g_host_base;
inline std::filesystem::path g_cdrom_base;
inline std::filesystem::path g_host_cwd;
inline std::filesystem::path g_cdrom_cwd;
inline std::string g_ps2_cwd_device = "host0";

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

inline std::mutex g_sif_module_mutex;
inline std::unordered_map<int32_t, SifModuleRecord> g_sif_modules_by_id;
inline std::unordered_map<std::string, int32_t> g_sif_module_id_by_path;
inline int32_t g_next_sif_module_id = 1;
inline uint32_t g_sif_module_log_count = 0;
