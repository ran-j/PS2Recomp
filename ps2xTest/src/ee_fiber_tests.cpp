#include "MiniTest.h"
#include "runtime/ee_fiber.h"

#include <cmath>
#include <string>
#include <vector>

// The scheduler suspends guest stacks on these instead of unwinding them, so
// what matters is that a live C++ call chain -- locals, destructors, an
// in-flight exception -- survives being parked and resumed.
namespace
{
    struct Marker
    {
        int *counter;
        explicit Marker(int *c) : counter(c) {}
        ~Marker() { ++*counter; }
    };

    struct Ctx
    {
        EeFiber *fiber = nullptr;
        long rounds = 0;
        long observed = 0;
        int destructed = 0;
        bool caught = false;
        bool deepOk = false;
        double fp = 0.0;
        std::string payload;
    };

    struct Boom
    {
    };

    void deepFrames(Ctx *c, int depth)
    {
        Marker marker(&c->destructed);
        std::string local = "frame-" + std::to_string(depth);
        if (depth > 0)
        {
            deepFrames(c, depth - 1);
            c->deepOk = c->deepOk && local == ("frame-" + std::to_string(depth));
            return;
        }
        c->deepOk = true;
        for (int i = 0; i < 4; ++i)
        {
            c->payload = "suspended-at-depth";
            c->fiber->suspend();
            c->deepOk = c->deepOk && c->payload == "suspended-at-depth";
        }
    }

    void fiberBody(void *user)
    {
        auto *c = static_cast<Ctx *>(user);
        for (long i = 0; i < c->rounds; ++i)
        {
            ++c->observed;
            c->fiber->suspend();
        }
        deepFrames(c, 32);
        c->fiber->suspend();
        try
        {
            Marker marker(&c->destructed);
            throw Boom{};
        }
        catch (const Boom &)
        {
            c->caught = true;
        }
        c->fiber->suspend();
        c->fp = 0.0;
        for (int i = 0; i < 8; ++i)
        {
            c->fp += std::sqrt(static_cast<double>(i + 1));
            c->fiber->suspend();
        }
        for (;;)
        {
            c->fiber->suspend();
        }
    }
} // namespace

void register_ee_fiber_tests()
{
    MiniTest::Case("EeFiber", [](TestCase &tc)
    {
        tc.Run("a suspended fiber keeps its C++ stack alive", [](TestCase &t)
        {
            EeFiber fiber;
            Ctx ctx;
            ctx.fiber = &fiber;
            ctx.rounds = 256;
            t.IsTrue(fiber.create(fiberBody, &ctx, 1u << 20u), "create succeeds");
            t.IsTrue(fiber.valid(), "valid after create");

            for (long i = 0; i < ctx.rounds; ++i)
                fiber.resume();
            t.Equals(ctx.rounds, ctx.observed, "every round trip ran exactly once");

            for (int i = 0; i < 5; ++i)
                fiber.resume();
            t.IsTrue(ctx.deepOk, "a 32-frame call chain survived being suspended");
            t.IsTrue(ctx.payload == "suspended-at-depth", "locals intact across suspend");

            const int beforeThrow = ctx.destructed;
            fiber.resume();
            t.IsTrue(ctx.caught, "an exception is caught inside the fiber");
            t.IsTrue(ctx.destructed > beforeThrow, "unwinding ran destructors on the fiber stack");

            for (int i = 0; i < 8; ++i)
                fiber.resume();
            double expected = 0.0;
            for (int i = 0; i < 8; ++i)
                expected += std::sqrt(static_cast<double>(i + 1));
            t.IsTrue(std::fabs(ctx.fp - expected) < 1e-9, "FP state survives switching");
        });

        tc.Run("many fibers interleave without disturbing each other", [](TestCase &t)
        {
            constexpr int kCount = 32;
            std::vector<EeFiber> fibers(kCount);
            std::vector<Ctx> ctxs(kCount);
            bool created = true;
            for (int i = 0; i < kCount; ++i)
            {
                ctxs[i].fiber = &fibers[i];
                ctxs[i].rounds = 40;
                created = created && fibers[i].create(fiberBody, &ctxs[i], 256u * 1024u);
            }
            t.IsTrue(created, "all stacks allocated");

            for (int round = 0; round < 40; ++round)
                for (int i = 0; i < kCount; ++i)
                    fibers[i].resume();

            bool allRan = true;
            for (int i = 0; i < kCount; ++i)
                allRan = allRan && ctxs[i].observed == 40;
            t.IsTrue(allRan, "each fiber advanced exactly its own count");
        });
    });
}
