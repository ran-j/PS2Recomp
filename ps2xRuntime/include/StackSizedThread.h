#pragma once

#include <functional>
#include <memory>
#include <stdexcept>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <process.h>

extern "C"
{
    typedef void *HANDLE;
    __declspec(dllimport) unsigned long WINAPI WaitForSingleObject(HANDLE hHandle, unsigned long dwMilliseconds);
    __declspec(dllimport) int WINAPI CloseHandle(HANDLE hObject);
}
#elif defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#endif

// Secondary OS threads default to a platform-specific stack size that can be
// far smaller than a single recompiled PS2 function's native stack frame
// needs: macOS defaults a pthread's stack to 512KB, which large switch/goto
// based recompiled functions (plus their logging/debug instrumentation) can
// exceed after only a couple of nested calls, producing a hard-to-diagnose
// stack overflow. This launches a thread with an explicit, generous stack
// size on every supported platform instead of relying on that OS default.
class StackSizedThread
{
public:
    StackSizedThread() = default;

    explicit StackSizedThread(std::function<void()> fn, size_t stackSizeBytes = 16u * 1024u * 1024u)
    {
        auto *fnPtr = new std::function<void()>(std::move(fn));

#if defined(_WIN32)
        m_handle = reinterpret_cast<void *>(::_beginthreadex(
            nullptr, static_cast<unsigned>(stackSizeBytes), &StackSizedThread::trampoline, fnPtr, 0, nullptr));
        if (!m_handle)
        {
            delete fnPtr;
            throw std::runtime_error("StackSizedThread: failed to create thread");
        }
#else
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, stackSizeBytes);
        const int rc = pthread_create(&m_thread, &attr, &StackSizedThread::trampoline, fnPtr);
        pthread_attr_destroy(&attr);
        if (rc != 0)
        {
            delete fnPtr;
            throw std::runtime_error("StackSizedThread: failed to create thread");
        }
        m_valid = true;
#endif
    }

    StackSizedThread(const StackSizedThread &) = delete;
    StackSizedThread &operator=(const StackSizedThread &) = delete;

    StackSizedThread(StackSizedThread &&other) noexcept
    {
        *this = std::move(other);
    }

    StackSizedThread &operator=(StackSizedThread &&other) noexcept
    {
        if (this != &other)
        {
            join();
#if defined(_WIN32)
            m_handle = other.m_handle;
            other.m_handle = nullptr;
#else
            m_thread = other.m_thread;
            m_valid = other.m_valid;
            other.m_valid = false;
#endif
        }
        return *this;
    }

    ~StackSizedThread()
    {
        join();
    }

    bool joinable() const noexcept
    {
#if defined(_WIN32)
        return m_handle != nullptr;
#else
        return m_valid;
#endif
    }

    void join()
    {
#if defined(_WIN32)
        if (m_handle)
        {
            ::WaitForSingleObject(m_handle, 0xFFFFFFFFu);
            ::CloseHandle(m_handle);
            m_handle = nullptr;
        }
#else
        if (m_valid)
        {
            pthread_join(m_thread, nullptr);
            m_valid = false;
        }
#endif
    }

private:
#if defined(_WIN32)
    static unsigned __stdcall trampoline(void *arg)
    {
        std::unique_ptr<std::function<void()>> fn(static_cast<std::function<void()> *>(arg));
        (*fn)();
        return 0;
    }
    void *m_handle = nullptr;
#else
    static void *trampoline(void *arg)
    {
        std::unique_ptr<std::function<void()>> fn(static_cast<std::function<void()> *>(arg));
        (*fn)();
        return nullptr;
    }
    pthread_t m_thread{};
    bool m_valid = false;
#endif
};
