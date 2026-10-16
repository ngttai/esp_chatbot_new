#include <cstdio>
#include "suspend_coordinator.hpp"

using C = SuspendCoordinator;
#define CHECK(test) do { if (!(test)) { std::fprintf(stderr, "Suspend check failed at %d: %s\n", __LINE__, #test); return 1; } } while (false)

int main()
{
    C c(2000, 2000, 5000);
    CHECK(!c.begin(0));
    CHECK(!c.register_participant(0) && !c.register_participant(9));
    for (uint8_t id = 1; id <= 8; ++id) CHECK(c.register_participant(id));
    CHECK(!c.register_participant(1));
    CHECK(c.snapshot().registered == 255);
    for (uint8_t id = 4; id <= 8; ++id) CHECK(c.unregister_participant(id));
    CHECK(c.snapshot().registered == 7);
    CHECK(c.begin(0));
    const auto first = c.snapshot().session;
    CHECK(!c.register_participant(4) && !c.unregister_participant(1));
    CHECK(!c.report(first + 1, 1, C::Report::Ready));
    CHECK(!c.report(first, 4, C::Report::Ready));
    CHECK(!c.report(first, 1, C::Report::Resumed));
    CHECK(c.report(first, 1, C::Report::Ready));
    CHECK(c.report(first, 1, C::Report::Ready));
    CHECK(c.poll(50, false) == C::Command::None);
    CHECK(c.report(first, 2, C::Report::Ready));
    CHECK(c.report(first, 3, C::Report::Ready));
    CHECK(c.poll(100, false) == C::Command::Commit);
    CHECK(!c.report(first, 1, C::Report::Ready));
    CHECK(c.report(first, 1, C::Report::Suspended));
    CHECK(c.poll(200, false) == C::Command::None);
    CHECK(c.report(first, 2, C::Report::Suspended));
    CHECK(c.report(first, 3, C::Report::Suspended));
    CHECK(c.poll(250, false) == C::Command::Suspended);
    CHECK(c.poll(300, true) == C::Command::Resume);
    CHECK(c.report(first, 1, C::Report::Resumed));
    CHECK(c.poll(350, false) == C::Command::None);
    CHECK(c.report(first, 2, C::Report::Resumed));
    CHECK(c.report(first, 3, C::Report::Resumed));
    CHECK(c.poll(400, false) == C::Command::Resumed);
    CHECK(c.snapshot().phase == C::Phase::Idle && c.snapshot().resume_to_active);

    // A negative vote rolls back every participant, including ones not yet Ready.
    CHECK(c.begin(500));
    const auto second = c.snapshot().session;
    CHECK(second != first && !c.report(first, 1, C::Report::Ready));
    CHECK(c.report(second, 1, C::Report::Busy));
    CHECK(c.poll(550, false) == C::Command::Cancel);
    CHECK(!c.report(second, 2, C::Report::Ready));
    for (uint8_t id = 1; id <= 3; ++id) CHECK(c.report(second, id, C::Report::Resumed));
    CHECK(c.poll(600, false) == C::Command::Resumed);
    CHECK(c.snapshot().phase == C::Phase::Backoff);
    CHECK(!c.begin(601));
    CHECK(c.poll(5599, false) == C::Command::None && c.snapshot().phase == C::Phase::Backoff);
    CHECK(c.poll(5600, false) == C::Command::None && c.snapshot().phase == C::Phase::Idle);

    CHECK(c.begin(6000));
    const auto third = c.snapshot().session;
    CHECK(c.poll(7999, false) == C::Command::None);
    CHECK(c.poll(8000, false) == C::Command::Cancel);
    CHECK(c.snapshot().reason == C::Reason::PrepareTimeout);
    CHECK(!c.report(third, 1, C::Report::Ready));
    CHECK(c.poll(10000, false) == C::Command::Recovery);
    CHECK(c.snapshot().reason == C::Reason::ResumeTimeout);
    CHECK(!c.report(third, 1, C::Report::Resumed));
    CHECK(!c.begin(10001) && !c.unregister_participant(1));
    CHECK(c.retry_resume(10050) == C::Command::Resume);
    const auto recovery = c.snapshot().session;
    CHECK(recovery != third && !c.report(third, 1, C::Report::Resumed));
    for (uint8_t id = 1; id <= 3; ++id) CHECK(c.report(recovery, id, C::Report::Resumed));
    CHECK(c.poll(10100, false) == C::Command::Resumed && c.snapshot().phase == C::Phase::Idle);

    // Activity takes priority over all-ready, so no Commit is sent.
    CHECK(c.begin(11000));
    for (uint8_t id = 1; id <= 3; ++id) CHECK(c.report(c.snapshot().session, id, C::Report::Ready));
    CHECK(c.poll(11050, true) == C::Command::Cancel);
    for (uint8_t id = 1; id <= 3; ++id) CHECK(c.report(c.snapshot().session, id, C::Report::Resumed));
    CHECK(c.poll(11100, false) == C::Command::Resumed && c.snapshot().phase == C::Phase::Idle);

    // Once Commit was sent, wake is latched until suspend completion, then Resume.
    CHECK(c.begin(12000));
    for (uint8_t id = 1; id <= 3; ++id) CHECK(c.report(c.snapshot().session, id, C::Report::Ready));
    CHECK(c.poll(12050, false) == C::Command::Commit);
    CHECK(c.poll(12100, true) == C::Command::None);
    for (uint8_t id = 1; id <= 3; ++id) CHECK(c.report(c.snapshot().session, id, C::Report::Suspended));
    CHECK(c.poll(12150, false) == C::Command::Resume);
    for (uint8_t id = 1; id <= 3; ++id) CHECK(c.report(c.snapshot().session, id, C::Report::Resumed));
    CHECK(c.poll(12200, false) == C::Command::Resumed);

    // Commit timeout requires confirmed Resume; it cannot declare Active itself.
    CHECK(c.begin(13000));
    for (uint8_t id = 1; id <= 3; ++id) CHECK(c.report(c.snapshot().session, id, C::Report::Ready));
    CHECK(c.poll(13050, false) == C::Command::Commit);
    CHECK(c.poll(15050, false) == C::Command::Resume);
    CHECK(c.snapshot().phase == C::Phase::Resuming && c.snapshot().reason == C::Reason::CommitTimeout);
    c.interrupt();
    CHECK(c.snapshot().phase == C::Phase::Recovery && c.snapshot().reason == C::Reason::Stopped);
    CHECK(c.retry_resume(15100) == C::Command::Resume);
    for (uint8_t id = 1; id <= 3; ++id) CHECK(c.report(c.snapshot().session, id, C::Report::Resumed));
    CHECK(c.poll(15150, false) == C::Command::Resumed);

    CHECK(c.begin(16000));
    CHECK(c.report(c.snapshot().session, 1, C::Report::Error));
    CHECK(c.poll(16050, false) == C::Command::Cancel);
    CHECK(c.snapshot().reason == C::Reason::Error);
    for (uint8_t id = 1; id <= 3; ++id) CHECK(c.report(c.snapshot().session, id, C::Report::Resumed));
    CHECK(c.poll(16100, true) == C::Command::Resumed);
    CHECK(c.begin(17000));
    for (uint8_t id = 1; id <= 3; ++id) CHECK(c.report(c.snapshot().session, id, C::Report::Ready));
    CHECK(c.poll(17050, false) == C::Command::Commit);
    CHECK(c.report(c.snapshot().session, 2, C::Report::Error));
    CHECK(c.poll(17100, false) == C::Command::Resume);
    for (uint8_t id = 1; id <= 3; ++id) CHECK(c.report(c.snapshot().session, id, C::Report::Resumed));
    CHECK(c.poll(17150, false) == C::Command::Resumed);

    // Unsigned elapsed time handles clock wrap; cycling never reallocates the table.
    for (unsigned cycle = 0; cycle < 10000; ++cycle) {
        const uint32_t start = UINT32_MAX - 999;
        CHECK(c.begin(start));
        CHECK(c.poll(999, false) == C::Command::None);
        CHECK(c.poll(1000, false) == C::Command::Cancel);
        for (uint8_t id = 1; id <= 3; ++id) CHECK(c.report(c.snapshot().session, id, C::Report::Resumed));
        CHECK(c.poll(1001, true) == C::Command::Resumed);
        CHECK(c.snapshot().phase == C::Phase::Idle);
    }
    std::printf("Suspend protocol passed: 8 slots, lifecycle, stale/duplicate responses, deadlines, rollback, wake, wrap and 10000 cycles; coordinator=%zu bytes\n", sizeof(C));
}
