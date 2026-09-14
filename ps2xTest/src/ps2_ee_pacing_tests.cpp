#include "MiniTest.h"
#include "runtime/ee_scheduler.h"
#include "EeHostPacing.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <memory>
#include <tuple>
#include <vector>

using Clock = ee_host_pacing::Clock;
using namespace std::chrono_literals;

struct EeSchedulerPacingTestAccess {
    struct Deadline {
        uint64_t cycle, sequence;
        Clock::time_point host;
        EeEventType type;
        uint32_t id;
    };
    static void cycle(EeScheduler &scheduler, uint64_t value) { scheduler.m_eeCycle = value; }
    static uint64_t cycle(const EeScheduler &scheduler) { return scheduler.m_eeCycle; }
    static void pump(EeScheduler &scheduler) { scheduler.processDueDeadlines(); }
    static void wait(EeScheduler &scheduler) { scheduler.waitForEvent(); }
    static void clear(EeScheduler &scheduler) {
        scheduler.m_deadlines.clear();
        scheduler.m_eventSequence = 0;
        scheduler.m_pendingInvocations.clear();
        scheduler.m_invocationSequence = 0;
        scheduler.updateNextDeadline();
    }
    static void add(EeScheduler &scheduler, uint64_t cycle, Clock::time_point host, EeEventType type, uint32_t id = 0) {
        scheduler.scheduleEvent(cycle, host, {type, id, 0});
    }
    static std::vector<Deadline> deadlines(const EeScheduler &scheduler) {
        std::vector<Deadline> result;
        for (const auto &item : scheduler.m_deadlines)
            result.push_back({item.deadlineCycle, item.sequence, item.hostDeadline, item.event.type, item.event.id});
        std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.cycle < b.cycle; });
        return result;
    }
    static void alarm(EeScheduler &scheduler, int id, uint32_t handler) {
        EeAlarm alarm{};
        alarm.id = id;
        alarm.handler = handler;
        scheduler.m_alarms.emplace(id, alarm);
    }
    static std::vector<uint32_t> invoked(const EeScheduler &scheduler) {
        std::vector<uint32_t> result;
        for (const auto &item : scheduler.m_pendingInvocations) result.push_back(item.context.pc);
        return result;
    }
};

namespace {
constexpr auto period = 16667us;
constexpr auto duration = 500us;
constexpr uint64_t cycles(uint64_t microseconds) {
    return (microseconds * EeScheduler::kEeClockHz + 999999u) / 1000000u;
}
constexpr uint64_t periodCycles = cycles(16667), durationCycles = cycles(500);
constexpr uint32_t startHandler = 0x1000, endHandler = 0x1100, alarmHandler = 0x1200;
void handler(uint8_t *, R5900Context *, PS2Runtime *) {}

struct FakeClock {
    Clock::time_point time{100s};
    std::vector<Clock::time_point> waits;
    ee_host_pacing::ClockHooks hooks{this,
        [](void *context) { return static_cast<FakeClock *>(context)->time; },
        [](void *context, Clock::time_point deadline) {
            auto &self = *static_cast<FakeClock *>(context);
            self.waits.push_back(deadline);
            self.time = std::max(self.time, deadline);
        }};
    FakeClock() { ee_host_pacing::clockHooks = &hooks; }
    ~FakeClock() { ee_host_pacing::clockHooks = nullptr; }
};

struct Fixture {
    FakeClock clock;
    std::unique_ptr<PS2Runtime> runtime = std::make_unique<PS2Runtime>();
    EeScheduler &scheduler = runtime->eeScheduler();
    R5900Context context{};
    bool initialize() {
        if (!runtime->memory().initialize()) return false;
        runtime->registerFunction(startHandler, handler);
        runtime->registerFunction(endHandler, handler);
        runtime->registerFunction(alarmHandler, handler);
        scheduler.reset(runtime->memory().getRDRAM(), context);
        scheduler.addIrqHandler(false, 2, startHandler, true, 0, 0, 0);
        scheduler.addIrqHandler(false, 3, endHandler, true, 0, 0, 0);
        return true;
    }
};
}

int main() {
    MiniTest::Case("EeHostPacing", [](TestCase &tc) {
        tc.Run("on-time fields retain every deadline and event", [](TestCase &t) {
            Fixture fixture;
            t.IsTrue(fixture.initialize(), "fixture initializes");
            const auto epoch = fixture.clock.time;
            for (uint64_t field = 1; field <= 120; ++field) {
                EeSchedulerPacingTestAccess::cycle(fixture.scheduler, field * periodCycles);
                EeSchedulerPacingTestAccess::pump(fixture.scheduler);
                const auto deadlines = EeSchedulerPacingTestAccess::deadlines(fixture.scheduler);
                t.Equals(fixture.scheduler.currentVSyncTick(), field, "exactly one tick per start");
                t.IsTrue(fixture.clock.time == epoch + field * period, "steady host cadence retains reset epoch");
                t.Equals(EeSchedulerPacingTestAccess::cycle(fixture.scheduler), field * periodCycles, "pacing does not alter guest cycles");
                t.Equals(deadlines.size(), size_t{2}, "only one end and one start remain");
                t.Equals(deadlines[0].cycle, field * periodCycles + durationCycles, "end guest deadline unchanged");
                t.Equals(deadlines[1].cycle, (field + 1) * periodCycles, "next-start guest deadline unchanged");
                t.Equals(deadlines[0].sequence, field * 2u, "end sequence unchanged");
                t.Equals(deadlines[1].sequence, field * 2u + 1u, "start sequence unchanged");
                t.IsTrue(deadlines[0].host == epoch + field * period + duration, "end uses same start boundary");
                t.IsTrue(deadlines[1].host == epoch + (field + 1) * period, "next start uses same start boundary");
            }
            auto invoked = EeSchedulerPacingTestAccess::invoked(fixture.scheduler);
            t.Equals(invoked.size(), size_t{239}, "no start or completed end was lost");
            for (size_t i = 0; i < invoked.size(); ++i)
                t.Equals(invoked[i], i % 2 == 0 ? startHandler : endHandler, "start/end order retained");
        });
        tc.Run("jitter tolerates exactly one field and rebases beyond it", [](TestCase &t) {
            for (const auto late : {0us, 500us, period, period + 1us, 10s + 0us}) {
                Fixture fixture;
                t.IsTrue(fixture.initialize(), "fixture initializes");
                const auto scheduled = fixture.clock.time + period;
                fixture.clock.time = scheduled + late;
                EeSchedulerPacingTestAccess::cycle(fixture.scheduler, periodCycles);
                EeSchedulerPacingTestAccess::pump(fixture.scheduler);
                const auto deadlines = EeSchedulerPacingTestAccess::deadlines(fixture.scheduler);
                const auto boundary = late > period ? fixture.clock.time - period : scheduled;
                t.IsTrue(deadlines[0].host == boundary + duration, "matching end follows selected host boundary");
                t.IsTrue(deadlines[1].host == boundary + period, "next start follows selected host boundary");
                t.Equals(deadlines[0].cycle, periodCycles + durationCycles, "jitter never shifts end guest cycle");
                t.Equals(deadlines[1].cycle, periodCycles * 2, "jitter never shifts start guest cycle");
            }
        });
        tc.Run("long stall cannot cause sustained accelerated recovery", [](TestCase &t) {
            Fixture fixture;
            t.IsTrue(fixture.initialize(), "fixture initializes");
            fixture.clock.time += 10s;
            const auto recovery = fixture.clock.time;
            for (uint64_t field = 1; field <= 180; ++field) {
                EeSchedulerPacingTestAccess::cycle(fixture.scheduler, field * periodCycles);
                EeSchedulerPacingTestAccess::pump(fixture.scheduler);
                t.Equals(fixture.scheduler.currentVSyncTick(), field, "all overdue guest starts still occur");
                const auto elapsedFields = field > 1u ? field - 2u : 0u;
                t.IsTrue(fixture.clock.time == recovery + elapsedFields * period,
                         "at most one catch-up field precedes unchanged field spacing");
            }
            const double elapsed = std::chrono::duration<double>(fixture.clock.time - recovery).count();
            t.IsTrue(178.0 / elapsed <= 60.0, "after one catch-up field recovery stays at the guest rate");
        });
        tc.Run("already-due end start and alarms retain batch ordering", [](TestCase &t) {
            for (unsigned variant = 0; variant < 4; ++variant) {
                Fixture fixture;
                t.IsTrue(fixture.initialize(), "fixture initializes");
                const auto epoch = fixture.clock.time;
                EeSchedulerPacingTestAccess::clear(fixture.scheduler);
                // A previous end, the current start and alarms are all due when
                // the guest returns after a stall. Keep their original batch order.
                struct Input { uint64_t cycle; EeEventType type; uint32_t id; };
                std::array<Input, 5> input{{
                    {periodCycles - durationCycles, EeEventType::VBlankEnd, 0},
                    {periodCycles - 1, EeEventType::Alarm, 1},
                    {periodCycles, EeEventType::VBlankStart, 0},
                    {periodCycles, EeEventType::Alarm, 2},
                    {periodCycles + (variant & 1 ? durationCycles + 1 : 1), EeEventType::Alarm, 3}}};
                if (variant & 2) std::reverse(input.begin(), input.end());
                for (const auto &item : input) {
                    if (item.type == EeEventType::Alarm)
                        EeSchedulerPacingTestAccess::alarm(fixture.scheduler, item.id, alarmHandler + item.id * 0x10);
                    EeSchedulerPacingTestAccess::add(fixture.scheduler, item.cycle, epoch + period, item.type, item.id);
                }
                fixture.clock.time += 10s;
                const auto recovery = fixture.clock.time;
                const auto guestCycle = periodCycles + (variant & 1 ? durationCycles + 1 : 1);
                EeSchedulerPacingTestAccess::cycle(fixture.scheduler, guestCycle);
                EeSchedulerPacingTestAccess::pump(fixture.scheduler);
                auto expected = std::vector<uint32_t>{endHandler, alarmHandler + 0x10, startHandler,
                                                       alarmHandler + 0x20, alarmHandler + 0x30};
                if (variant & 1) expected.push_back(endHandler);
                t.IsTrue(EeSchedulerPacingTestAccess::invoked(fixture.scheduler) == expected, "mixed due batch and subsequently scheduled end preserve order");
                t.Equals(EeSchedulerPacingTestAccess::cycle(fixture.scheduler), guestCycle, "all event processing preserves guest cycle");
                t.Equals(fixture.scheduler.currentVSyncTick(), uint64_t{1}, "mixed events produce one start tick");
                const auto remaining = EeSchedulerPacingTestAccess::deadlines(fixture.scheduler);
                t.IsTrue(remaining.back().host == recovery, "only one catch-up start remains after mixed overdue events");
                t.Equals(remaining.back().cycle, periodCycles * 2, "next-start guest deadline stays original");
                t.Equals(remaining.back().sequence, uint64_t{7}, "existing event sequence ids are not rewritten");
            }
        });
        tc.Run("new alarms keep call-relative host time across pacing rebase", [](TestCase &t) {
            Fixture fixture;
            t.IsTrue(fixture.initialize(), "fixture initializes");
            fixture.clock.time += 10s;
            EeSchedulerPacingTestAccess::cycle(fixture.scheduler, periodCycles);
            EeSchedulerPacingTestAccess::pump(fixture.scheduler);
            const auto callTime = fixture.clock.time;
            const int alarm = fixture.scheduler.setAlarm(1, alarmHandler, 0, 0, 0);
            t.IsTrue(alarm > 0, "alarm is installed");
            const auto beforeWait = EeSchedulerPacingTestAccess::deadlines(fixture.scheduler);
            t.IsTrue(beforeWait[0].host == callTime + 64us, "alarm host deadline is not rebased with VBlank");
            t.Equals(beforeWait[0].cycle, periodCycles + cycles(64), "alarm guest deadline is unchanged");
            EeSchedulerPacingTestAccess::wait(fixture.scheduler);
            t.IsTrue(fixture.clock.time == callTime + 64us, "idle scheduler waits only until alarm");
            t.Equals(EeSchedulerPacingTestAccess::cycle(fixture.scheduler), periodCycles + cycles(64), "idle wait advances exact guest alarm cycles");
            EeSchedulerPacingTestAccess::pump(fixture.scheduler);
            const auto invoked = EeSchedulerPacingTestAccess::invoked(fixture.scheduler);
            t.IsTrue(invoked == std::vector<uint32_t>{startHandler, alarmHandler}, "alarm expires before corresponding VBlank end");
            const auto afterWait = EeSchedulerPacingTestAccess::deadlines(fixture.scheduler);
            t.IsTrue(afterWait[0].host == callTime - period + duration, "alarm did not move VBlank end");
            t.IsTrue(afterWait[1].host == callTime, "alarm did not move next VBlank start");
        });
        tc.Run("slow two-field renders retain bounded catch-up without an added host wait", [](TestCase &t) {
            Fixture fixture;
            t.IsTrue(fixture.initialize(), "fixture initializes");
            for (uint64_t render = 0; render < 30u; ++render) {
                fixture.clock.time += 120ms;
                const auto ready = fixture.clock.time;
                for (uint64_t field = 1; field <= 2u; ++field) {
                    EeSchedulerPacingTestAccess::cycle(fixture.scheduler, (render * 2u + field) * periodCycles);
                    EeSchedulerPacingTestAccess::pump(fixture.scheduler);
                    t.IsTrue(fixture.clock.time == ready, "slow render owes no additional full-field host wait");
                }
                const auto pending = EeSchedulerPacingTestAccess::deadlines(fixture.scheduler);
                t.IsTrue(pending.back().host == ready + period,
                         "catch-up debt stays bounded after every slow render");
            }
        });
    });
    return MiniTest::Run();
}
