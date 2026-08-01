#ifndef PS2_LOG_H
#define PS2_LOG_H

#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#ifndef PS2_RUNTIME_LOGS
#define PS2_RUNTIME_LOGS 0
#endif

#ifndef AGRESSIVE_LOGS
#define AGRESSIVE_LOGS 0
#endif

namespace ps2_log
{
struct RuntimeLogEntry
{
    uint64_t seq = 0;
    std::string text;
};

inline constexpr size_t kMaxRuntimeLogEntries = 4096;

inline std::mutex &runtime_log_mutex()
{
    static std::mutex m;
    return m;
}

inline std::deque<RuntimeLogEntry> &runtime_log_entries()
{
    static std::deque<RuntimeLogEntry> entries;
    return entries;
}

inline uint64_t &runtime_log_next_seq()
{
    static uint64_t seq = 1;
    return seq;
}

inline bool &runtime_log_paused()
{
    static bool paused = false;
    return paused;
}

inline void set_runtime_log_paused(bool paused)
{
    std::lock_guard<std::mutex> lock(runtime_log_mutex());
    runtime_log_paused() = paused;
}

inline bool is_runtime_log_paused()
{
    std::lock_guard<std::mutex> lock(runtime_log_mutex());
    return runtime_log_paused();
}

inline void append_runtime_log_text(const std::string &text)
{
    if (text.empty())
    {
        return;
    }

    std::lock_guard<std::mutex> lock(runtime_log_mutex());
    if (runtime_log_paused())
    {
        return;
    }

    auto &entries = runtime_log_entries();
    RuntimeLogEntry entry{};
    entry.seq = runtime_log_next_seq()++;
    entry.text = text;
    entries.push_back(std::move(entry));

    while (entries.size() > kMaxRuntimeLogEntries)
    {
        entries.pop_front();
    }
}

inline std::vector<RuntimeLogEntry> snapshot_runtime_log_entries()
{
    std::lock_guard<std::mutex> lock(runtime_log_mutex());
    const auto &entries = runtime_log_entries();
    return std::vector<RuntimeLogEntry>(entries.begin(), entries.end());
}

inline void clear_runtime_log_entries()
{
    std::lock_guard<std::mutex> lock(runtime_log_mutex());
    runtime_log_entries().clear();
}
}

#if PS2_RUNTIME_LOGS || AGRESSIVE_LOGS
#define RUNTIME_LOG(x)                                                                                                  \
    do                                                                                                                  \
    {                                                                                                                   \
        std::ostringstream _ps2_runtime_log_stream;                                                                     \
        _ps2_runtime_log_stream << x;                                                                                   \
        const std::string _ps2_runtime_log_text = _ps2_runtime_log_stream.str();                                        \
        if (_ps2_runtime_log_text.empty())                                                                            \
        {                                                                                                               \
            std::cout.flush();                                                                                          \
        }                                                                                                               \
        else                                                                                                            \
        {                                                                                                               \
            std::cout << _ps2_runtime_log_text;                                                                         \
            ps2_log::append_runtime_log_text(_ps2_runtime_log_text);                                                    \
        }                                                                                                               \
    } while (0)
#else
#define RUNTIME_LOG(x) do {} while(0)
#endif

// Unlike RUNTIME_LOG, this is NOT gated by PS2_RUNTIME_LOGS/AGRESSIVE_LOGS: it
// always compiles in. Used exclusively by the PS2X_DIAG-gated observability
// probes ([gs:*], [watch], clut-dump, ...), which already pay for their own
// runtime gate (ps2_diag::enabled() / g_writeWatchActive) at each call site --
// making the log statement itself depend on a *second*, build-time-only gate
// would mean PS2X_DIAG=1 silently produces no output in a default (non
// PS2X_ENABLE_RUNTIME_LOGS) build, defeating the point of an opt-in,
// env-var-driven diagnostics layer.
#define PS2X_DIAG_LOG(x)                                                                                                  \
    do                                                                                                                    \
    {                                                                                                                     \
        std::ostringstream _ps2_diag_log_stream;                                                                          \
        _ps2_diag_log_stream << x;                                                                                        \
        const std::string _ps2_diag_log_text = _ps2_diag_log_stream.str();                                               \
        if (_ps2_diag_log_text.empty())                                                                                   \
        {                                                                                                                 \
            std::cout.flush();                                                                                           \
        }                                                                                                                 \
        else                                                                                                              \
        {                                                                                                                 \
            std::cout << _ps2_diag_log_text;                                                                              \
            ps2_log::append_runtime_log_text(_ps2_diag_log_text);                                                         \
        }                                                                                                                 \
    } while (0)

#if AGRESSIVE_LOGS

namespace ps2_log
{
inline std::string log_path()
{
    static std::string path;
    if (path.empty())
    {
        path = (std::filesystem::current_path() / "ps2_log.txt").string();
    }
    return path;
}
inline std::ostream &log_stream()
{
    static std::ofstream f(log_path(), std::ios::out);
    return f.is_open() ? f : std::cerr;
}
inline int &depth()
{
    static thread_local int d = 0;
    return d;
}
inline void log_entry(const char *name)
{
    for (int i = 0; i < depth(); ++i)
        log_stream() << '\t';
    log_stream() << ">> " << name << " enter\n";
    log_stream().flush();
    depth()++;
}
inline void log_exit(const char *name)
{
    depth()--;
    for (int i = 0; i < depth(); ++i)
        log_stream() << '\t';
    log_stream() << "<< " << name << " exit\n";
    log_stream().flush();
}
inline void print_saved_location()
{
    std::cout << "[PS2 LOG] Logs saved at " << log_path() << std::endl;
}
}

#define PS_LOG_ENTRY(name) \
    ps2_log::log_entry(name); \
    struct _ps2_log_guard_ { const char *_n; _ps2_log_guard_(const char *n) : _n(n) {} \
        ~_ps2_log_guard_() { ps2_log::log_exit(_n); } } _ps2_log_guard_(name)
#define PS2_IF_AGRESSIVE_LOGS(code) \
    do                              \
    {                               \
        code;                       \
    } while (0)

#else

namespace ps2_log
{
inline std::string log_path()
{
    return (std::filesystem::current_path() / "ps2_log.txt").string();
}
inline void print_saved_location() {}
}
#define PS_LOG_ENTRY(name) ((void)0)
#define PS2_IF_AGRESSIVE_LOGS(code) ((void)0)

#endif

#endif
