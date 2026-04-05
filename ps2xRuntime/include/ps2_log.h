#ifndef PS2_LOG_H
#define PS2_LOG_H

#include <fstream>
#include <iostream>
#include <filesystem>

#if defined(AGRESSIVE_LOGS)
#define PS2_AGRESSIVE_LOGS_ENABLED 1
#else
#define PS2_AGRESSIVE_LOGS_ENABLED 0
#endif

#if defined(_DEBUG)
#define RUNTIME_LOG(x) do { std::cout << x; } while (0)
#else
#define RUNTIME_LOG(x) do {} while(0)
#endif

#ifdef neverDone

namespace ps2_log
{
inline constexpr bool agressive_logs_enabled = PS2_AGRESSIVE_LOGS_ENABLED != 0;

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
        if constexpr (ps2_log::agressive_logs_enabled) \
        {                           \
            code;                   \
        }                           \
    } while (0)

#else

namespace ps2_log
{
inline constexpr bool agressive_logs_enabled = false;
inline void print_saved_location() {}
}
#define PS_LOG_ENTRY(name) ((void)0)
#define PS2_IF_AGRESSIVE_LOGS(code) ((void)0)

#endif

#endif
