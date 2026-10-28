#include <cstdio>
#include <cstdlib>
#include "modules/display/emote_render_owner.hpp"
#define CHECK(t) do { if (!(t)) { std::fprintf(stderr, "Emote render owner failed %d: %s\n", __LINE__, #t); std::abort(); } } while (false)

struct Fake final : EmoteRenderOwner::Backend {
    bool running = true, quiesced = false;
    bool busy = false;       // quiescing/resuming is refused while a frame is rendering
    bool broken = false;     // the renderer cannot be reached at all
    unsigned requests = 0, sets = 0;
    bool request_pause() override
    {
        ++requests;
        if (broken) return false;
        running = false;
        return true;
    }
    bool set_paused(bool paused) override
    {
        ++sets;
        if (broken || busy) return false;
        running = !paused;
        quiesced = paused;
        return true;
    }
};

int main()
{
    constexpr uint32_t T = EmoteRenderOwner::ACTIVATION_TIMEOUT_MS;
    {
        // Another source owns the display at start: paused at once, without waiting for quiescence.
        EmoteRenderOwner o; Fake f;
        f.busy = true;
        CHECK(o.update(0, f) && !f.running && !f.quiesced && f.requests == 1 && f.sets == 0);
        CHECK(o.update(20, f) && f.requests == 1);
        CHECK(o.paused());
    }
    {
        // Activation keeps the renderer paused until the queue barrier reports ready.
        EmoteRenderOwner o; Fake f;
        o.update(0, f);
        const uint32_t token = o.begin_activation(100);
        CHECK(token != 0);
        o.update(110, f); CHECK(!f.running);
        o.activation_ready(token);
        o.update(120, f); CHECK(f.running && !o.paused() && !o.take_timed_out());
        CHECK(o.begin_activation(130) == 0); // already owned
        o.update(140, f); CHECK(f.running);
    }
    {
        // A barrier that never runs cannot freeze the screen.
        EmoteRenderOwner o; Fake f;
        o.update(0, f);
        const uint32_t token = o.begin_activation(1000);
        CHECK(token != 0);
        o.update(1000 + T - 1, f); CHECK(!f.running && !o.take_timed_out());
        o.update(1000 + T, f); CHECK(f.running);
        CHECK(o.take_timed_out() && !o.take_timed_out());
        // The late barrier of the released activation changes nothing.
        o.activation_ready(token);
        CHECK(!o.take_timed_out());
        o.source_lost();
        o.activation_ready(token);
        o.update(2000 + T, f); CHECK(!f.running);
    }
    {
        // Timer wrap-around does not release early.
        EmoteRenderOwner o; Fake f;
        o.update(0, f);
        CHECK(o.begin_activation(0xFFFFFFF0u) != 0);
        o.update(5, f); CHECK(!f.running);
        o.update(0xFFFFFFF0u + T, f); CHECK(f.running);
    }
    {
        // A stale barrier from an earlier activation is ignored.
        EmoteRenderOwner o; Fake f;
        o.update(0, f);
        const uint32_t first = o.begin_activation(10);
        o.source_lost();
        const uint32_t second = o.begin_activation(20);
        CHECK(second != 0 && second != first);
        o.activation_ready(first); o.update(30, f); CHECK(!f.running);
        o.activation_ready(second); o.update(40, f); CHECK(f.running);
        o.source_lost(); o.activation_ready(second); o.update(50, f); CHECK(!f.running);
    }
    {
        // Losing the source while owned pauses at once even when the renderer is busy.
        EmoteRenderOwner o; Fake f;
        o.update(0, f);
        o.activation_ready(o.begin_activation(1)); o.update(2, f); CHECK(f.running);
        f.busy = true;
        o.source_lost();
        CHECK(o.update(3, f) && !f.running);
        o.activation_ready(1); o.update(4, f); CHECK(!f.running);
    }
    {
        // Returning while the pause was only requested resumes once the renderer can be reached.
        EmoteRenderOwner o; Fake f;
        f.busy = true;
        o.update(0, f);
        o.activation_ready(o.begin_activation(1));
        CHECK(!o.update(2, f) && !f.running);
        f.busy = false;
        CHECK(o.update(22, f) && f.running && !o.paused());
    }
    {
        // Power pause and source pause are independent causes.
        EmoteRenderOwner o; Fake f;
        o.update(0, f);
        o.activation_ready(o.begin_activation(1)); o.update(2, f); CHECK(f.running);
        CHECK(o.set_power_paused(true, 3, f) && !f.running && f.quiesced);
        CHECK(o.set_power_paused(false, 4, f) && f.running);
        // Power resume while another source owns the display must not restart the renderer.
        o.source_lost(); o.update(5, f); CHECK(!f.running);
        CHECK(o.set_power_paused(true, 6, f) && f.quiesced);
        const unsigned sets = f.sets;
        CHECK(o.set_power_paused(false, 7, f) && !f.running && f.sets == sets);
        // Source activation while power holds the pause must not restart it either.
        CHECK(o.set_power_paused(true, 8, f));
        o.activation_ready(o.begin_activation(9)); o.update(10, f); CHECK(!f.running);
        CHECK(o.set_power_paused(false, 11, f) && f.running);
    }
    {
        // A power pause needs a quiesced renderer: a source-only pause is not enough.
        EmoteRenderOwner o; Fake f;
        o.update(0, f);
        f.busy = true;
        CHECK(!o.set_power_paused(true, 1, f) && !f.quiesced);
        f.busy = false;
        CHECK(o.set_power_paused(true, 2, f) && f.quiesced);
        CHECK(o.set_power_paused(false, 3, f) && f.quiesced); // source still blocks, stays paused
    }
    {
        // An unreachable renderer is retried and reported to the power protocol.
        EmoteRenderOwner o; Fake f;
        f.broken = true;
        CHECK(!o.update(0, f) && f.running && !o.paused());
        CHECK(!o.set_power_paused(true, 1, f) && f.running);
        f.broken = false;
        CHECK(o.update(2, f) && !o.take_timed_out());
        CHECK(o.set_power_paused(true, 3, f) && f.quiesced);
        o.activation_ready(o.begin_activation(4));
        CHECK(o.set_power_paused(false, 5, f) && f.running);
        f.broken = true;
        o.source_lost();
        CHECK(!o.update(6, f) && f.running);
        f.broken = false;
        CHECK(o.update(7, f) && !f.running);
    }
    return 0;
}
