#include "MiniTest.h"
#include "runtime/vu/vu_pipeline_events.h"

#include <algorithm>
#include <vector>

namespace
{
    using ps2vu::PipelineEvents;
    using ps2vu::WritebackKind;

    struct Event
    {
        uint64_t cycle;
        uint32_t slot;
    };

    template <WritebackKind Kind> void compareSchedule(TestCase &t)
    {
        PipelineEvents events;
        std::vector<Event> pending;
        uint32_t random = 0x9E3779B9u;
        for (uint64_t cycle = 0; cycle < 8192; ++cycle)
        {
            uint32_t expected = 0;
            for (const auto &event : pending)
                if (event.cycle == cycle)
                    expected |= 1u << event.slot;
            t.Equals(PipelineEvents::slots<Kind>(events.take(cycle)), expected,
                     "only events due at this cycle should be delivered");
            std::erase_if(pending, [cycle](const Event &event) { return event.cycle == cycle; });

            for (uint32_t slot = 0; slot < PipelineEvents::capacity(Kind); ++slot)
            {
                random = random * 1664525u + 1013904223u;
                if (((random >> 24) & 3u) != 0 ||
                    std::any_of(pending.begin(), pending.end(),
                                [slot](const Event &event) { return event.slot == slot; }))
                    continue;
                const uint32_t latency = 1u + ((random >> 8) % 63u);
                events.schedule<Kind>(cycle, latency, slot);
                pending.push_back({cycle + latency, slot});
            }

            uint64_t next = PipelineEvents::noEvent;
            for (const auto &event : pending)
                next = std::min(next, event.cycle);
            t.Equals(events.nextCycle(cycle), next, "next event must match the ordered reference");
            if (cycle % 257u == 256u)
            {
                events.reset();
                pending.clear();
                t.Equals(events.nextCycle(cycle), PipelineEvents::noEvent, "reset must discard pending work");
            }
        }
    }
}

void register_ps2_vu_pipeline_tests()
{
    MiniTest::Case("VU pipeline events", [](TestCase &tc) {
        tc.Run("mixed latencies match an ordered reference across wraps and resets", [](TestCase &t) {
            compareSchedule<WritebackKind::Flag>(t);
            compareSchedule<WritebackKind::Fdiv>(t);
            compareSchedule<WritebackKind::Efu>(t);
            compareSchedule<WritebackKind::Store>(t);
            compareSchedule<WritebackKind::Vf>(t);
            compareSchedule<WritebackKind::Vi>(t);
            compareSchedule<WritebackKind::Acc>(t);
        });

        tc.Run("simultaneous writebacks retain every pipeline and slot", [](TestCase &t) {
            PipelineEvents events;
            const auto schedule = [&]<WritebackKind Kind>() {
                for (uint32_t slot = 0; slot < PipelineEvents::capacity(Kind); ++slot)
                    events.schedule<Kind>(61, 4, slot);
            };
            schedule.operator()<WritebackKind::Flag>();
            schedule.operator()<WritebackKind::Fdiv>();
            schedule.operator()<WritebackKind::Efu>();
            schedule.operator()<WritebackKind::Store>();
            schedule.operator()<WritebackKind::Vf>();
            schedule.operator()<WritebackKind::Vi>();
            schedule.operator()<WritebackKind::Acc>();
            t.Equals(events.nextCycle(61), uint64_t(65), "first deadline must cross the calendar boundary");
            t.Equals(events.take(64), uint64_t(0), "nothing may commit early");
            const uint64_t ready = events.take(65);
            const auto check = [&]<WritebackKind Kind>() {
                t.Equals(PipelineEvents::slots<Kind>(ready), (1u << PipelineEvents::capacity(Kind)) - 1u,
                         "no simultaneous writeback may be lost or assigned to "
                         "another pipeline");
            };
            check.operator()<WritebackKind::Flag>();
            check.operator()<WritebackKind::Fdiv>();
            check.operator()<WritebackKind::Efu>();
            check.operator()<WritebackKind::Store>();
            check.operator()<WritebackKind::Vf>();
            check.operator()<WritebackKind::Vi>();
            check.operator()<WritebackKind::Acc>();
            t.Equals(events.nextCycle(65), PipelineEvents::noEvent, "drained schedule must be empty");
            t.Equals(events.take(129), uint64_t(0), "a wrapped bucket must not replay old events");
        });

        tc.Run("invalid events fail without changing the schedule", [](TestCase &t) {
            PipelineEvents events;
            for (const uint32_t latency : {0u, 64u, 65u})
            {
                bool rejected = false;
                try
                {
                    events.schedule<WritebackKind::Vf>(0, latency, 0);
                }
                catch (const std::logic_error &)
                {
                    rejected = true;
                }
                t.IsTrue(rejected, "an invalid latency must be rejected");
            }
            bool rejected = false;
            try
            {
                events.schedule<WritebackKind::Vf>(0, 4, 16);
            }
            catch (const std::logic_error &)
            {
                rejected = true;
            }
            t.IsTrue(rejected, "an invalid slot must be rejected");
            t.Equals(events.nextCycle(0), PipelineEvents::noEvent, "invalid events must leave no pending work");
        });
    });
}
