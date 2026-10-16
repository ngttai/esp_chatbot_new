#include <cstdio>
#include <cstdlib>
#include <vector>
#include "modules/display/panel_power.hpp"
#define CHECK(t) do { if (!(t)) { std::fprintf(stderr, "Panel power failed %d: %s\n", __LINE__, #t); std::abort(); } } while (false)
using P = PanelPower;
using C = P::Command;
using R = P::Result;
struct Bus final : P::Backend {
    std::vector<C> commands;
    bool black = false, fail_blackout = false;
    int fail_command = -1;
    bool blackout() override { if (fail_blackout) return false; black = true; return true; }
    bool command(C value) override
    {
        CHECK(black);
        commands.push_back(value);
        if (static_cast<int>(value) == fail_command) { fail_command = -1; return false; }
        return true;
    }
};
int main()
{
    {
        P panel; Bus bus;
        CHECK(panel.resume(0, bus) == R::Ready && bus.commands.empty());
        CHECK(panel.suspend(0, bus) == R::Pending);
        CHECK(bus.black && bus.commands == std::vector<C>{C::DisplayOff});
        CHECK(panel.suspend(1, bus) == R::Pending);
        CHECK(bus.commands.back() == C::SleepIn);
        CHECK(panel.suspend(5, bus) == R::Pending);
        CHECK(panel.suspend(6, bus) == R::Ready);
        CHECK(panel.suspend(7, bus) == R::Ready && bus.commands.size() == 2);
        CHECK(panel.resume(8, bus) == R::Pending && bus.commands.back() == C::SleepOut);
        CHECK(panel.resume(127, bus) == R::Pending && bus.commands.size() == 3);
        CHECK(panel.resume(128, bus) == R::Ready && bus.commands.back() == C::DisplayOn);
        CHECK(panel.resume(129, bus) == R::Ready && bus.commands.size() == 4);
    }
    {
        P panel; Bus bus;
        // Cancel before SLPIN only needs DISPON; it must not send a new sleep command.
        CHECK(panel.suspend(0, bus) == R::Pending);
        CHECK(panel.resume(1, bus) == R::Ready);
        CHECK((bus.commands == std::vector<C>{C::DisplayOff, C::DisplayOn}));
    }
    {
        P panel; Bus bus;
        CHECK(panel.suspend(0, bus) == R::Pending);
        CHECK(panel.suspend(1, bus) == R::Pending);
        CHECK(panel.resume(5, bus) == R::Pending && bus.commands.size() == 2);
        CHECK(panel.resume(6, bus) == R::Pending && bus.commands.back() == C::SleepOut);
        CHECK(panel.resume(126, bus) == R::Ready);
    }
    for (C fail : {C::DisplayOff, C::SleepIn, C::SleepOut, C::DisplayOn}) {
        P panel; Bus bus;
        uint32_t failed_at = 0;
        bus.fail_command = static_cast<int>(fail);
        if (fail == C::DisplayOff) {
            CHECK(panel.suspend(0, bus) == R::Error);
        } else {
            CHECK(panel.suspend(0, bus) == R::Pending);
            if (fail == C::SleepIn) { failed_at = 1; CHECK(panel.suspend(1, bus) == R::Error); }
            else {
                CHECK(panel.suspend(1, bus) == R::Pending);
                CHECK(panel.suspend(6, bus) == R::Ready);
                if (fail == C::SleepOut) { failed_at = 10; CHECK(panel.resume(10, bus) == R::Error); }
                else {
                    CHECK(panel.resume(10, bus) == R::Pending);
                    CHECK(panel.resume(130, bus) == R::Error);
                    failed_at = 130;
                }
            }
        }
        const auto count = bus.commands.size();
        CHECK(panel.resume(failed_at + 119, bus) == R::Pending && bus.commands.size() == count);
        // Fault retries recover conservatively with SLPOUT and a fresh full 120ms interval.
        CHECK(panel.resume(failed_at + 120, bus) == R::Pending);
        CHECK(bus.commands.size() == count + 1 && bus.commands.back() == C::SleepOut);
        CHECK(panel.resume(failed_at + 240, bus) == R::Ready && bus.commands.back() == C::DisplayOn);
    }
    {
        P panel; Bus bus; bus.fail_blackout = true;
        CHECK(panel.suspend(0, bus) == R::Error && bus.commands.empty());
    }
    {
        P panel; Bus bus;
        const uint32_t now = UINT32_MAX - 3;
        CHECK(panel.suspend(now, bus) == R::Pending);
        CHECK(panel.suspend(now + 1, bus) == R::Pending);
        CHECK(panel.resume(now + 5, bus) == R::Pending && bus.commands.size() == 2);
        CHECK(panel.resume(now + 6, bus) == R::Pending && bus.commands.back() == C::SleepOut);
        CHECK(panel.resume(now + 126, bus) == R::Ready);
    }
    {
        P panel; Bus bus;
        for (uint32_t i = 0; i < 1000; ++i) {
            const auto now = i * 1000;
            CHECK(panel.suspend(now, bus) == R::Pending);
            CHECK(panel.suspend(now + 1, bus) == R::Pending);
            CHECK(panel.suspend(now + 6, bus) == R::Ready);
            CHECK(panel.resume(now + 10, bus) == R::Pending);
            CHECK(panel.resume(now + 129, bus) == R::Pending);
            CHECK(panel.resume(now + 130, bus) == R::Ready);
            CHECK(bus.commands.size() == (i + 1) * 4);
        }
    }
    std::printf("Panel power PASS: %zu bytes, no worker delays\n", sizeof(P));
}
