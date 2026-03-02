#ifndef PS2_LOG_H
#define PS2_LOG_H

#include <fstream>
#include <iostream>
#include <filesystem>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

#ifdef _DEBUG

namespace ps2_log
{
inline std::string log_path()
{
    static std::string path;
    if (path.empty())
    {
#if defined(_WIN32)
        char buf[MAX_PATH];
        if (GetModuleFileNameA(nullptr, buf, sizeof(buf)))
            path = (std::filesystem::path(buf).parent_path() / "ps2_log.txt").string();
#endif
        if (path.empty())
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

#else

namespace ps2_log
{
inline void print_saved_location() {}
}
#define PS_LOG_ENTRY(name) ((void)0)

#endif

#endif
