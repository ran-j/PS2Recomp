#include "runtime/gs/gs_threaded_backend.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cfenv>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <variant>
#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#endif

struct GSThreadedBackend::Impl
{
    struct PresentationResult
    {
        std::mutex mutex;
        std::condition_variable ready;
        bool completed = false;
        GSPresentationTicket frame;
        std::exception_ptr error;
        void fail(std::exception_ptr failure)
        {
            std::lock_guard lock(mutex);
            if (completed) return;
            error = failure;
            completed = true;
            ready.notify_all();
        }
    };
    struct PresentationTicket final : GSPreparedPresentation
    {
        std::shared_ptr<const int> owner;
        std::shared_ptr<PresentationResult> result;
        PresentationTicket(std::shared_ptr<const int> owner, std::shared_ptr<PresentationResult> result)
            : owner(std::move(owner)), result(std::move(result)) {}
    };
    struct Prepare
    {
        GSPresentationRequest request;
        std::shared_ptr<PresentationResult> result;
    };
    enum class Op { Submit, Transfer, Upload, Flush, TextureFlush, Write, Prepare };
    using Pixel = std::array<uint32_t, 6>;
    struct Command
    {
        Op op;
        std::variant<std::monostate, GSPrimitiveBatch, GSTransferCommand,
                     std::vector<uint8_t>, Pixel, Prepare> data;
        int rounding = std::fegetround();
        size_t bytes() const
        {
            return sizeof(Command) + (op == Op::Upload ? std::get<std::vector<uint8_t>>(data).size() : 0u);
        }
    };

    explicit Impl(std::unique_ptr<GSRasterBackend> target, size_t queueBytes)
        : backend(std::move(target)), capacity(std::max(queueBytes, sizeof(Command)))
    {
        if (!backend)
            throw std::invalid_argument("GS worker requires a backend");
        worker = std::thread([this] { run(); });
    }

    ~Impl()
    {
        backend->CancelPreparedPresentations();
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        work.notify_one();
        worker.join();
    }

    void enqueue(Command command, bool flush = false)
    {
        // A presenter may also submit. Holding this lock across a drain keeps
        // newer commands behind the synchronous operation, not merely its fence.
        std::lock_guard submitLock(submission);
        const size_t size = command.bytes();
        std::unique_lock lock(mutex);
        if (outstandingBytes != 0u && size > capacity - std::min(capacity, outstandingBytes))
        {
            urgent = true;
            work.notify_one();
            progress.wait(lock, [&] {
                return error || outstandingBytes == 0u ||
                       size <= capacity - std::min(capacity, outstandingBytes);
            });
        }
        rethrow();
        pending.push_back(std::move(command));
        outstandingBytes += size;
        urgent = urgent || flush;
        if (pending.size() == 1u || pending.size() == 64u || urgent)
            work.notify_one();
    }

    template <class F>
    auto observe(F &&fn)
    {
        std::lock_guard submitLock(submission);
        {
            std::unique_lock lock(mutex);
            urgent = true;
            work.notify_one();
            progress.wait(lock, [&] { return outstandingBytes == 0u || error; });
            rethrow();
        }
        return fn(*backend);
    }

    void rethrow() const
    {
        if (error)
            std::rethrow_exception(error);
    }

    void execute(const Command &command)
    {
        // XGKICK submits while the VU has selected rounding toward zero.
        if (rounding != command.rounding)
        {
            std::fesetround(command.rounding);
            rounding = command.rounding;
        }
        switch (command.op)
        {
        case Op::Submit: backend->Submit(std::get<GSPrimitiveBatch>(command.data)); break;
        case Op::Transfer: backend->BeginTransfer(std::get<GSTransferCommand>(command.data)); break;
        case Op::Upload:
        {
            const auto &bytes = std::get<std::vector<uint8_t>>(command.data);
            backend->UploadImage(bytes.data(), static_cast<uint32_t>(bytes.size()));
            break;
        }
        case Op::Flush: backend->Flush(); break;
        case Op::TextureFlush: backend->TextureFlush(); break;
        case Op::Write:
        {
            const auto &p = std::get<Pixel>(command.data);
            backend->WriteVram(p[0], p[1], p[2], p[3], p[4], p[5]);
            break;
        }
        case Op::Prepare:
        {
            const auto &prepare = std::get<Prepare>(command.data);
            backend->Flush();
            backend->Sync(GSSyncReason::Presentation);
            auto frame = backend->PreparePresentation(prepare.request);
            if (!frame)
                throw std::runtime_error("GS presentation preparation returned no ticket");
            std::lock_guard lock(prepare.result->mutex);
            prepare.result->frame = std::move(frame);
            prepare.result->completed = true;
            prepare.result->ready.notify_all();
            break;
        }
        }
    }

    void run()
    {
        rounding = std::fegetround();
#if defined(__APPLE__)
        pthread_setname_np("GSWorker");
#elif defined(__linux__)
        pthread_setname_np(pthread_self(), "GSWorker");
#endif
        std::vector<Command> batch;
        for (;;)
        {
            size_t bytes = 0u;
            {
                std::unique_lock lock(mutex);
                work.wait(lock, [&] { return stopping || !pending.empty(); });
                if (pending.empty() && stopping)
                    return;
                // Batch small primitives without stranding a short final packet.
                work.wait_for(lock, std::chrono::milliseconds(1), [&] {
                    return stopping || urgent || pending.size() >= 64u;
                });
                batch.swap(pending);
                urgent = false;
            }
            try
            {
                for (const auto &command : batch)
                {
                    execute(command);
                    bytes += command.bytes();
                }
            }
            catch (...)
            {
                std::lock_guard lock(mutex);
                error = std::current_exception();
                for (const auto &command : batch)
                    if (command.op == Op::Prepare)
                        std::get<Prepare>(command.data).result->fail(error);
                for (const auto &command : pending)
                    if (command.op == Op::Prepare)
                        std::get<Prepare>(command.data).result->fail(error);
                pending.clear();
                outstandingBytes = 0u;
                progress.notify_all();
                return;
            }
            batch.clear();
            {
                std::lock_guard lock(mutex);
                outstandingBytes -= bytes;
            }
            progress.notify_all();
        }
    }

    std::unique_ptr<GSRasterBackend> backend;
    const std::shared_ptr<const int> identity = std::make_shared<const int>(0);
    const size_t capacity;
    std::mutex submission, mutex;
    std::condition_variable work, progress;
    std::vector<Command> pending;
    size_t outstandingBytes = 0u;
    bool urgent = false, stopping = false;
    std::exception_ptr error;
    int rounding = FE_TONEAREST;
    std::thread worker;
};

GSThreadedBackend::GSThreadedBackend(std::unique_ptr<GSRasterBackend> backend, size_t queueBytes)
    : m_impl(std::make_unique<Impl>(std::move(backend), queueBytes)) {}
GSThreadedBackend::~GSThreadedBackend() = default;

void GSThreadedBackend::Initialize(uint8_t *vram, uint32_t size)
{
    m_impl->observe([&](auto &b) { b.Initialize(vram, size); });
}
void GSThreadedBackend::Reset() { m_impl->observe([](auto &b) { b.Reset(); }); }
void GSThreadedBackend::Submit(const GSPrimitiveBatch &batch)
{
    m_impl->enqueue({Impl::Op::Submit, batch});
}
void GSThreadedBackend::BeginTransfer(const GSTransferCommand &command)
{
    m_impl->enqueue({Impl::Op::Transfer, command});
}
void GSThreadedBackend::UploadImage(const uint8_t *data, uint32_t size)
{
    std::vector<uint8_t> bytes;
    if (size != 0u)
    {
        if (!data)
            throw std::invalid_argument("GS upload data is null");
        bytes.assign(data, data + size);
    }
    m_impl->enqueue({Impl::Op::Upload, std::move(bytes)});
}
void GSThreadedBackend::Flush() { m_impl->enqueue({Impl::Op::Flush, {}}, true); }
void GSThreadedBackend::TextureFlush() { m_impl->enqueue({Impl::Op::TextureFlush, {}}); }
void GSThreadedBackend::Sync(GSSyncReason reason)
{
    m_impl->observe([&](auto &b) { b.Sync(reason); });
}
PresentationFrame GSThreadedBackend::Present(const GSPresentationRequest &request)
{
    if (SupportsPreparedPresentation())
        return DisplayPreparedPresentation(PreparePresentation(request));
    return m_impl->observe([&](auto &b) { return b.Present(request); });
}
bool GSThreadedBackend::SupportsPreparedPresentation() const
{
    return m_impl->backend->SupportsPreparedPresentation();
}
GSPresentationTicket GSThreadedBackend::PreparePresentation(const GSPresentationRequest &request)
{
    if (!SupportsPreparedPresentation()) return {};
    auto result = std::make_shared<Impl::PresentationResult>();
    auto ticket = std::make_shared<Impl::PresentationTicket>(m_impl->identity, result);
    ticket->sourceVsyncTick = request.vsyncTick;
    m_impl->enqueue({Impl::Op::Prepare, Impl::Prepare{request, std::move(result)}}, true);
    return ticket;
}
PresentationFrame GSThreadedBackend::DisplayPreparedPresentation(const GSPresentationTicket &ticket)
{
    const auto *prepared = dynamic_cast<const Impl::PresentationTicket *>(ticket.get());
    if (!prepared || prepared->owner != m_impl->identity)
        throw std::invalid_argument("GS presentation ticket belongs to another backend");
    GSPresentationTicket frame;
    {
        std::unique_lock lock(prepared->result->mutex);
        prepared->result->ready.wait(lock, [&] { return prepared->result->completed; });
        if (prepared->result->error) std::rethrow_exception(prepared->result->error);
        frame = prepared->result->frame;
    }
    return m_impl->backend->DisplayPreparedPresentation(frame);
}
void GSThreadedBackend::CancelPreparedPresentations() noexcept
{
    m_impl->backend->CancelPreparedPresentations();
}
bool GSThreadedBackend::ClearFramebuffer(const GSContext &context, uint32_t rgba)
{
    return m_impl->observe([&](auto &b) { return b.ClearFramebuffer(context, rgba); });
}
uint32_t GSThreadedBackend::ConsumeLocalToHostBytes(uint8_t *dst, uint32_t size)
{
    return m_impl->observe([&](auto &b) { return b.ConsumeLocalToHostBytes(dst, size); });
}
uint32_t GSThreadedBackend::ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const
{
    return m_impl->observe([&](auto &b) { return b.ReadVram(psm, base, bw, x, y); });
}
void GSThreadedBackend::WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value)
{
    m_impl->enqueue({Impl::Op::Write, Impl::Pixel{psm, base, bw, x, y, value}});
}
void GSThreadedBackend::SnapshotVram(std::vector<uint8_t> &out) const
{
    m_impl->observe([&](auto &b) { b.SnapshotVram(out); });
}
GSTransferSnapshot GSThreadedBackend::GetTransferSnapshot() const
{
    return m_impl->observe([](auto &b) { return b.GetTransferSnapshot(); });
}
