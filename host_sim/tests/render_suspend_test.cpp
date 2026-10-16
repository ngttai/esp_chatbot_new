#include <cstdio>
#include <cstdlib>
#include "modules/display/render_suspend.hpp"
#define CHECK(t) do { if (!(t)) { std::fprintf(stderr, "Render suspend failed %d: %s\n", __LINE__, #t); std::abort(); } } while (false)
using C = SuspendCoordinator;
struct Fake final : RenderSuspend::Backend {
    C coordinator{100, 100, 50};
    bool gate = true, lvgl = false, gfx = false, flush_pending = false, full_frame = false;
    bool lvgl_busy = false, gfx_busy = false, allow_open = true;
    uint32_t source = 1, armed = 0;
    unsigned pause_calls = 0, reports = 0;
    Fake() { for (uint8_t id = 1; id <= 3; ++id) CHECK(coordinator.register_participant(id)); }
    bool admission(bool enabled) override { if (enabled && !allow_open) return false; gate = enabled; return true; }
    bool pause_lvgl(bool value) override { ++pause_calls; if (lvgl_busy) return false; lvgl = value; return true; }
    bool pause_gfx(bool value) override { ++pause_calls; if (gfx_busy) return false; gfx = value; return true; }
    bool drained() override { return !gate && !flush_pending; }
    uint32_t active_source() override { return source; }
    void arm_redraw(uint32_t id) override { CHECK(drained()); armed = id; full_frame = false; }
    bool redraw_complete() override { return full_frame; }
    void report(uint32_t session, uint8_t id, C::Report status) override
    {
        ++reports;
        CHECK(coordinator.report(session, id, status));
    }
    void update(RenderSuspend &adapter) { adapter.update(coordinator.snapshot(), *this); }
};
int main()
{
    {
        Fake f; RenderSuspend a;
        CHECK(f.coordinator.begin(0));
        f.gfx_busy = true; f.flush_pending = true;
        f.update(a);
        CHECK(!f.gate && f.lvgl && !f.gfx && f.coordinator.snapshot().ready == 1);
        f.update(a); CHECK(f.reports == 1);
        f.gfx_busy = false; f.update(a);
        CHECK(f.coordinator.snapshot().ready == 3);
        CHECK(f.coordinator.poll(1, true) == C::Command::Cancel);
        f.update(a); CHECK(!f.gate && f.coordinator.snapshot().resumed == 0);
        f.flush_pending = false; f.update(a);
        CHECK(f.gate && !f.lvgl && !f.gfx && f.coordinator.snapshot().resumed == 3);
        CHECK(f.coordinator.poll(2, true) == C::Command::None);
        f.full_frame = true; f.update(a);
        CHECK(f.coordinator.poll(3, true) == C::Command::Resumed);
    }
    {
        Fake f; RenderSuspend a;
        CHECK(f.coordinator.begin(0)); f.update(a);
        CHECK(f.coordinator.poll(1, false) == C::Command::Commit); f.update(a);
        CHECK(f.coordinator.poll(2, false) == C::Command::Suspended);
        auto calls = f.pause_calls;
        f.update(a); CHECK(f.pause_calls == calls && !f.gate && f.lvgl && f.gfx);
        CHECK(f.coordinator.poll(3, true) == C::Command::Resume); f.update(a);
        CHECK(f.coordinator.snapshot().resumed == 3);
        // A changed source must get its own full redraw, not reuse the previous fence.
        f.source = 2; f.full_frame = true; f.update(a);
        CHECK(f.coordinator.snapshot().resumed == 3);
        f.update(a); CHECK(f.armed == 2 && !f.full_frame);
        CHECK(f.coordinator.poll(103, true) == C::Command::Recovery);
        const auto old_session = f.coordinator.snapshot().session;
        calls = f.pause_calls; f.update(a); CHECK(f.pause_calls == calls);
        CHECK(f.coordinator.retry_resume(104) == C::Command::Resume);
        f.update(a); CHECK(f.coordinator.snapshot().session != old_session);
        CHECK(!f.coordinator.report(old_session, 3, C::Report::Resumed));
        f.full_frame = true; f.update(a);
        CHECK(f.coordinator.poll(105, true) == C::Command::Resumed);
    }
    {
        Fake f; RenderSuspend a;
        CHECK(f.coordinator.begin(0)); f.lvgl_busy = true; f.update(a);
        CHECK(f.coordinator.poll(100, false) == C::Command::Cancel);
        f.lvgl_busy = false; f.allow_open = false; f.update(a);
        CHECK(!f.gate && f.lvgl && f.gfx && f.coordinator.snapshot().resumed == 0);
        f.allow_open = true; f.update(a); f.full_frame = true; f.update(a);
        CHECK(f.coordinator.poll(101, false) == C::Command::Resumed);
        CHECK(f.coordinator.snapshot().phase == C::Phase::Backoff);
    }
    {
        Fake f; RenderSuspend a;
        for (uint32_t i = 0; i < 1000; ++i) {
            const uint32_t now = i * 10;
            CHECK(f.coordinator.begin(now)); f.update(a);
            CHECK(f.coordinator.poll(now + 1, false) == C::Command::Commit); f.update(a);
            CHECK(f.coordinator.poll(now + 2, false) == C::Command::Suspended);
            CHECK(f.coordinator.poll(now + 3, true) == C::Command::Resume); f.update(a);
            f.full_frame = true; f.update(a);
            CHECK(f.coordinator.poll(now + 4, true) == C::Command::Resumed);
        }
    }
    RedrawFence fence;
    fence.arm(1, 360, 360);
    fence.complete(2, 0, 0, 360, 360); CHECK(!fence.done());
    fence.complete(1, 0, 180, 360, 180); CHECK(!fence.done());
    fence.complete(1, 0, 0, 359, 360); CHECK(!fence.done());
    fence.complete(1, 0, 0, 360, 120); CHECK(!fence.done());
    fence.complete(1, 0, 100, 360, 140); CHECK(!fence.done());
    fence.complete(1, 0, 240, 360, 121); CHECK(!fence.done());
    fence.complete(1, 0, 240, 360, 120); CHECK(fence.done());
    fence.arm(2, 360, 360); CHECK(!fence.done());
    std::printf("Render suspend PASS; adapter=%zu bytes, fence=%zu bytes\n", sizeof(RenderSuspend), sizeof(RedrawFence));
}
