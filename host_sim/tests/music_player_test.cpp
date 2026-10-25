#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "modules/music/music_player.hpp"

using namespace music;

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition); \
    return 1; } } while (false)

namespace {

struct FakeBackend : PlayerBackend {
    std::vector<std::string> calls;
    bool fail_play = false;
    bool fail_pause = false;
    bool fail_resume = false;
    bool fail_stop = false;
    Player *sync_player = nullptr;
    uint64_t last[4] = {};
    bool accept(BackendCommand c, uint64_t seq) { last[size_t(c)] = seq; return true; }

    bool play(const std::string &path, uint64_t seq) override
    {
        calls.push_back("play " + path);
        if (fail_play) return false;
        accept(BackendCommand::Play, seq);
        if (sync_player != nullptr) sync_player->on_backend_state(BackendState::Playing);
        return true;
    }
    bool pause(uint64_t seq) override
    {
        calls.push_back("pause");
        return !fail_pause && accept(BackendCommand::Pause, seq);
    }
    bool resume(uint64_t seq) override
    {
        calls.push_back("resume");
        return !fail_resume && accept(BackendCommand::Resume, seq);
    }
    bool stop(uint64_t seq) override
    {
        calls.push_back("stop");
        if (fail_stop) return false;
        accept(BackendCommand::Stop, seq);
        if (sync_player != nullptr) sync_player->on_backend_state(BackendState::Idle);
        return true;
    }
};

struct Rig {
    FakeBackend backend;
    uint64_t now = 1000;
    Player player;
    std::vector<size_t> ended;

    explicit Rig(Player::Config config = {}): player(backend, withClock(std::move(config)))
    {
        player.set_ended_handler([this](size_t index) { ended.push_back(index); });
    }

    Player::Config withClock(Player::Config config)
    {
        config.now_ms = [this] { return now; };
        return config;
    }

    void start(size_t index = 0, uint32_t duration_ms = 10000)
    {
        player.play(index, "/sdcard/Music/t.mp3", duration_ms);
        player.on_backend_state(BackendState::Playing);
    }
};

int test_initial_state()
{
    Rig rig;
    const PlayerSnapshot snap = rig.player.snapshot();
    CHECK(snap.state == PlayerState::Stopped);
    CHECK(snap.elapsed_ms == 0);
    CHECK(!rig.player.pause());
    CHECK(!rig.player.resume());
    CHECK(rig.player.stop());
    CHECK(rig.backend.calls.empty());
    return 0;
}

int test_play_and_elapsed()
{
    Rig rig;
    CHECK(rig.player.play(3, "/sdcard/Music/a.mp3", 5000));
    CHECK(rig.backend.calls.size() == 1 && rig.backend.calls[0] == "play /sdcard/Music/a.mp3");
    PlayerSnapshot snap = rig.player.snapshot();
    CHECK(snap.state == PlayerState::Preparing);
    CHECK(snap.index == 3 && snap.duration_ms == 5000);

    rig.now += 700;
    CHECK(rig.player.snapshot().elapsed_ms == 0);

    rig.player.on_backend_state(BackendState::Playing);
    CHECK(rig.player.snapshot().state == PlayerState::Playing);
    rig.now += 1500;
    CHECK(rig.player.snapshot().elapsed_ms == 1500);
    rig.now += 60000;
    CHECK(rig.player.snapshot().elapsed_ms == 5000);
    return 0;
}

int test_late_duration()
{
    Rig rig;
    rig.start(2, 0);
    rig.now += 2000;
    CHECK(rig.player.snapshot().duration_ms == 0);
    rig.player.set_duration(5, 9000); // another track
    CHECK(rig.player.snapshot().duration_ms == 0);
    rig.player.set_duration(2, 8000);
    PlayerSnapshot snap = rig.player.snapshot();
    CHECK(snap.duration_ms == 8000 && snap.elapsed_ms == 2000);
    rig.player.set_duration(2, 1000); // already known
    CHECK(rig.player.snapshot().duration_ms == 8000);
    rig.now += 60000;
    CHECK(rig.player.snapshot().elapsed_ms == 8000);
    rig.player.on_backend_state(BackendState::Idle);
    snap = rig.player.snapshot();
    CHECK(snap.state == PlayerState::Ended && snap.elapsed_ms == 8000);

    // Stopped player ignores a late duration.
    Rig idle;
    idle.player.set_duration(0, 4000);
    CHECK(idle.player.snapshot().duration_ms == 0);
    return 0;
}

int test_late_duration_across_switch()
{
    Rig rig;
    rig.start(1, 0);
    rig.now += 3000;
    // Switch to track 2 (waits for Idle of track 1).
    CHECK(rig.player.play(2, "/b.mp3", 0));
    // Late metadata of the old track must not touch the new one.
    rig.player.set_duration(1, 7000);
    CHECK(rig.player.snapshot().index == 2 && rig.player.snapshot().duration_ms == 0);
    // Metadata of the new track arrives while it still waits.
    rig.player.set_duration(2, 6000);
    CHECK(rig.player.snapshot().duration_ms == 6000);
    rig.player.on_backend_state(BackendState::Idle);
    PlayerSnapshot snap = rig.player.snapshot();
    CHECK(snap.state == PlayerState::Preparing && snap.index == 2 && snap.duration_ms == 6000);
    rig.player.on_backend_state(BackendState::Playing);
    rig.now += 1500;
    snap = rig.player.snapshot();
    CHECK(snap.state == PlayerState::Playing && snap.elapsed_ms == 1500 && snap.duration_ms == 6000);

    // Metadata of track 2 arrives in Preparing; the later Playing keeps it.
    Rig prep;
    CHECK(prep.player.play(4, "/c.mp3", 0));
    prep.player.set_duration(4, 5000);
    CHECK(prep.player.snapshot().duration_ms == 5000);
    prep.player.on_backend_state(BackendState::Playing);
    prep.now += 1000;
    snap = prep.player.snapshot();
    CHECK(snap.duration_ms == 5000 && snap.elapsed_ms == 1000);

    // Rollback of a refused switch keeps a duration learned meanwhile.
    Rig back;
    back.start(1, 0);
    back.now += 2000;
    back.backend.fail_stop = true;
    CHECK(!back.player.play(2, "/b.mp3", 0));
    back.player.set_duration(1, 9000);
    snap = back.player.snapshot();
    CHECK(snap.index == 1 && snap.state == PlayerState::Playing && snap.duration_ms == 9000 &&
          snap.elapsed_ms == 2000);
    return 0;
}

int test_pause_resume()
{
    Rig rig;
    rig.start();
    rig.now += 2000;
    CHECK(rig.player.pause());
    CHECK(rig.player.snapshot().state == PlayerState::Paused);
    rig.now += 5000;
    CHECK(rig.player.snapshot().elapsed_ms == 2000);
    CHECK(rig.player.resume());
    CHECK(rig.player.snapshot().state == PlayerState::Playing);
    rig.now += 1000;
    CHECK(rig.player.snapshot().elapsed_ms == 3000);

    rig.backend.fail_pause = true;
    CHECK(!rig.player.pause());
    CHECK(rig.player.snapshot().state == PlayerState::Playing);
    rig.backend.fail_pause = false;
    CHECK(rig.player.pause());
    rig.backend.fail_resume = true;
    CHECK(!rig.player.resume());
    CHECK(rig.player.snapshot().state == PlayerState::Paused);

    rig.backend.fail_resume = false;
    rig.player.on_backend_state(BackendState::Paused);
    CHECK(rig.player.snapshot().state == PlayerState::Paused);
    CHECK(rig.player.resume());
    rig.player.on_backend_state(BackendState::Playing);
    CHECK(rig.player.snapshot().state == PlayerState::Playing);
    return 0;
}

int test_pause_not_allowed_while_preparing()
{
    Rig rig;
    rig.player.play(0, "/x.mp3", 1000);
    CHECK(!rig.player.pause());
    CHECK(rig.player.snapshot().state == PlayerState::Preparing);
    return 0;
}

int test_natural_end()
{
    Rig rig;
    rig.start(2, 4000);
    rig.now += 3900;
    rig.player.on_backend_state(BackendState::Idle);
    const PlayerSnapshot snap = rig.player.snapshot();
    CHECK(snap.state == PlayerState::Ended);
    CHECK(snap.elapsed_ms == 4000);
    CHECK(rig.ended.size() == 1 && rig.ended[0] == 2);

    Rig unknown;
    unknown.start(0, 0);
    unknown.now += 2500;
    unknown.player.on_backend_state(BackendState::Idle);
    CHECK(unknown.player.snapshot().state == PlayerState::Ended);
    CHECK(unknown.player.snapshot().elapsed_ms == 2500);
    return 0;
}

int test_short_playback_then_idle_is_ended()
{
    Rig rig;
    rig.start(5, 10000);
    rig.now += 200;
    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.player.snapshot().state == PlayerState::Ended);
    CHECK(rig.ended.size() == 1 && rig.ended[0] == 5);
    return 0;
}

int test_stop_ignores_idle()
{
    Rig rig;
    rig.start();
    rig.now += 3000;
    CHECK(rig.player.stop());
    CHECK(rig.backend.calls.back() == "stop");
    PlayerSnapshot snap = rig.player.snapshot();
    CHECK(snap.state == PlayerState::Stopped && snap.elapsed_ms == 0);
    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.player.snapshot().state == PlayerState::Stopped);
    CHECK(rig.ended.empty());
    const size_t calls = rig.backend.calls.size();
    CHECK(rig.player.stop());
    CHECK(rig.backend.calls.size() == calls);
    return 0;
}

int test_stop_failure_keeps_state()
{
    Rig rig;
    rig.start();
    rig.backend.fail_stop = true;
    CHECK(!rig.player.stop());
    CHECK(rig.player.snapshot().state == PlayerState::Playing);
    rig.backend.fail_stop = false;
    CHECK(rig.player.stop());
    CHECK(rig.player.snapshot().state == PlayerState::Stopped);
    return 0;
}

int test_stop_while_paused_and_after_end()
{
    Rig rig;
    rig.start();
    rig.now += 2000;
    CHECK(rig.player.pause());
    CHECK(rig.player.stop());
    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.player.snapshot().state == PlayerState::Stopped);

    rig.start();
    rig.now += 2000;
    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.player.snapshot().state == PlayerState::Ended);
    const size_t calls = rig.backend.calls.size();
    CHECK(rig.player.stop());
    CHECK(rig.backend.calls.size() == calls);
    CHECK(rig.player.snapshot().state == PlayerState::Stopped);
    return 0;
}

int test_switch_track_while_playing()
{
    Rig rig;
    rig.start(0, 10000);
    rig.now += 4000;
    CHECK(rig.player.play(1, "/b.mp3", 8000));
    CHECK(rig.backend.calls.back() == "stop");
    PlayerSnapshot snap = rig.player.snapshot();
    CHECK(snap.state == PlayerState::Preparing && snap.index == 1 && snap.elapsed_ms == 0);
    CHECK(snap.duration_ms == 8000);

    rig.player.on_backend_state(BackendState::Playing);
    CHECK(rig.player.snapshot().state == PlayerState::Preparing);
    CHECK(rig.backend.calls.back() == "stop");

    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.backend.calls.back() == "play /b.mp3");
    CHECK(rig.player.snapshot().state == PlayerState::Preparing);
    rig.player.on_backend_state(BackendState::Playing);
    snap = rig.player.snapshot();
    CHECK(snap.state == PlayerState::Playing && snap.index == 1 && snap.elapsed_ms == 0);

    rig.now += 8000;
    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.player.snapshot().state == PlayerState::Ended);
    CHECK(rig.ended.size() == 1 && rig.ended[0] == 1);
    return 0;
}

int test_switch_while_preparing_cancels_first()
{
    Rig rig;
    CHECK(rig.player.play(0, "/a.mp3", 1000));
    CHECK(rig.player.play(1, "/b.mp3", 2000));
    CHECK(rig.backend.calls.size() == 3);
    CHECK(rig.backend.calls[0] == "play /a.mp3");
    CHECK(rig.backend.calls[1] == "stop");
    CHECK(rig.backend.calls[2] == "play /b.mp3");
    PlayerSnapshot snap = rig.player.snapshot();
    CHECK(snap.state == PlayerState::Preparing && snap.index == 1 && snap.duration_ms == 2000);

    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.player.snapshot().state == PlayerState::Preparing);
    rig.player.on_backend_state(BackendState::Playing);
    snap = rig.player.snapshot();
    CHECK(snap.state == PlayerState::Playing && snap.index == 1);

    rig.now += 2000;
    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.player.snapshot().state == PlayerState::Ended);
    CHECK(rig.ended.size() == 1 && rig.ended[0] == 1);
    return 0;
}

int test_switch_while_preparing_without_late_event()
{
    Rig rig;
    CHECK(rig.player.play(0, "/a.mp3", 1000));
    CHECK(rig.player.play(1, "/b.mp3", 2000));
    rig.player.on_backend_state(BackendState::Playing);
    CHECK(rig.player.snapshot().state == PlayerState::Playing);
    rig.now += 2000;
    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.player.snapshot().state == PlayerState::Ended);
    CHECK(rig.ended.size() == 1);
    return 0;
}

int test_switch_while_preparing_stop_failure()
{
    Rig rig;
    CHECK(rig.player.play(0, "/a.mp3", 1000));
    rig.backend.fail_stop = true;
    CHECK(!rig.player.play(1, "/b.mp3", 2000));
    PlayerSnapshot snap = rig.player.snapshot();
    CHECK(snap.state == PlayerState::Preparing && snap.index == 0 && snap.duration_ms == 1000);
    CHECK(rig.backend.calls.size() == 2);
    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.player.snapshot().state == PlayerState::Error);

    rig.backend.fail_stop = false;
    CHECK(rig.player.play(1, "/b.mp3", 2000));
    CHECK(rig.backend.calls.back() == "play /b.mp3");
    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.player.snapshot().state == PlayerState::Error);
    return 0;
}

int test_stale_flag_does_not_outlive_preparing()
{
    Rig rig;
    CHECK(rig.player.play(0, "/a.mp3", 1000));
    CHECK(rig.player.play(1, "/b.mp3", 2000));
    rig.now += 6000;
    rig.player.tick();
    CHECK(rig.player.snapshot().state == PlayerState::Error);
    CHECK(rig.player.play(2, "/c.mp3", 3000));
    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.player.snapshot().state == PlayerState::Error);
    return 0;
}

int test_rapid_switches_keep_last()
{
    Rig rig;
    rig.start(0, 10000);
    CHECK(rig.player.play(1, "/b.mp3", 1000));
    CHECK(rig.player.play(2, "/c.mp3", 2000));
    CHECK(rig.player.play(3, "/d.mp3", 3000));
    size_t stops = 0;
    for (const std::string &call : rig.backend.calls) stops += call == "stop";
    CHECK(stops == 1);
    PlayerSnapshot snap = rig.player.snapshot();
    CHECK(snap.state == PlayerState::Preparing && snap.index == 3 && snap.duration_ms == 3000);

    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.backend.calls.back() == "play /d.mp3");
    for (const std::string &call : rig.backend.calls) CHECK(call != "play /b.mp3" && call != "play /c.mp3");
    rig.player.on_backend_state(BackendState::Playing);
    CHECK(rig.player.snapshot().index == 3 && rig.player.snapshot().state == PlayerState::Playing);
    return 0;
}

int test_switch_stop_failure_restores_state()
{
    Rig rig;
    rig.start(0, 10000);
    rig.now += 3000;
    rig.backend.fail_stop = true;
    CHECK(!rig.player.play(1, "/b.mp3", 5000));
    PlayerSnapshot snap = rig.player.snapshot();
    CHECK(snap.state == PlayerState::Playing && snap.index == 0 && snap.duration_ms == 10000);
    CHECK(snap.elapsed_ms == 3000);
    rig.now += 1000;
    CHECK(rig.player.snapshot().elapsed_ms == 4000);

    rig.backend.fail_stop = false;
    rig.backend.calls.clear();
    CHECK(rig.player.play(1, "/b.mp3", 5000));
    CHECK(rig.backend.calls.size() == 1 && rig.backend.calls[0] == "stop");
    return 0;
}

int test_switch_timeout_fails_without_starting()
{
    Rig rig;
    rig.start(0, 10000);
    CHECK(rig.player.play(1, "/b.mp3", 5000));
    rig.now += 1500;
    rig.player.tick();
    CHECK(rig.player.snapshot().state == PlayerState::Preparing);
    rig.now += 600;
    rig.player.tick();
    CHECK(rig.player.snapshot().state == PlayerState::Error);
    for (const std::string &call : rig.backend.calls) CHECK(call != "play /b.mp3");

    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.player.snapshot().state == PlayerState::Error);
    CHECK(rig.ended.empty());

    CHECK(rig.player.play(1, "/b.mp3", 5000));
    CHECK(rig.backend.calls.back() == "play /b.mp3");
    rig.player.on_backend_state(BackendState::Playing);
    CHECK(rig.player.snapshot().state == PlayerState::Playing);
    CHECK(rig.player.snapshot().index == 1);
    return 0;
}

int test_switch_timeout_then_retry_stops_again()
{
    Rig rig;
    rig.start(0, 10000);
    CHECK(rig.player.play(1, "/b.mp3", 5000));
    rig.now += 2500;
    rig.player.tick();
    CHECK(rig.player.snapshot().state == PlayerState::Error);

    rig.backend.calls.clear();
    CHECK(rig.player.play(2, "/c.mp3", 4000));
    CHECK(rig.backend.calls.size() == 1 && rig.backend.calls[0] == "stop");
    CHECK(rig.player.snapshot().state == PlayerState::Preparing);
    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.backend.calls.back() == "play /c.mp3");

    Rig playing;
    playing.start(0, 10000);
    CHECK(playing.player.play(1, "/b.mp3", 5000));
    playing.now += 2500;
    playing.player.tick();
    playing.backend.calls.clear();
    playing.player.on_backend_state(BackendState::Playing);
    CHECK(playing.backend.calls.size() == 1 && playing.backend.calls[0] == "stop");
    return 0;
}

int test_stop_cancels_pending_switch()
{
    Rig rig;
    rig.start(0, 10000);
    CHECK(rig.player.play(1, "/b.mp3", 5000));
    CHECK(rig.player.stop());
    CHECK(rig.player.snapshot().state == PlayerState::Stopped);
    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.player.snapshot().state == PlayerState::Stopped);
    for (const std::string &call : rig.backend.calls) CHECK(call != "play /b.mp3");
    return 0;
}

int test_play_right_after_stop_waits_for_idle()
{
    Rig rig;
    rig.start(0, 10000);
    CHECK(rig.player.stop());
    const size_t calls = rig.backend.calls.size();
    CHECK(rig.player.play(2, "/c.mp3", 4000));
    CHECK(rig.backend.calls.size() == calls);
    CHECK(rig.player.snapshot().state == PlayerState::Preparing);
    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.backend.calls.back() == "play /c.mp3");
    rig.player.on_backend_state(BackendState::Playing);
    CHECK(rig.player.snapshot().state == PlayerState::Playing && rig.player.snapshot().index == 2);
    return 0;
}

int test_natural_end_during_switch_request()
{
    Rig rig;
    rig.start(0, 3000);
    rig.now += 3000;
    CHECK(rig.player.play(1, "/b.mp3", 5000));
    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.backend.calls.back() == "play /b.mp3");
    CHECK(rig.ended.empty());
    return 0;
}

int test_switch_while_paused()
{
    Rig rig;
    rig.start();
    rig.now += 2000;
    CHECK(rig.player.pause());
    CHECK(rig.player.play(4, "/c.mp3", 3000));
    CHECK(rig.player.snapshot().state == PlayerState::Preparing);
    CHECK(!rig.player.pause());
    rig.player.on_backend_state(BackendState::Idle);
    rig.player.on_backend_state(BackendState::Playing);
    CHECK(rig.player.snapshot().state == PlayerState::Playing);
    CHECK(rig.player.snapshot().index == 4);
    return 0;
}

int test_play_failure()
{
    Rig rig;
    rig.backend.fail_play = true;
    CHECK(!rig.player.play(0, "/bad.mp3", 1000));
    CHECK(rig.player.snapshot().state == PlayerState::Error);
    rig.backend.fail_play = false;
    CHECK(rig.player.play(1, "/ok.mp3", 1000));
    rig.player.on_backend_state(BackendState::Playing);
    CHECK(rig.player.snapshot().state == PlayerState::Playing);
    return 0;
}

int test_idle_while_preparing_is_error()
{
    Rig rig;
    CHECK(rig.player.play(0, "/missing.mp3", 1000));
    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.player.snapshot().state == PlayerState::Error);
    CHECK(rig.ended.empty());
    return 0;
}

int test_prepare_timeout()
{
    Rig rig;
    CHECK(rig.player.play(0, "/slow.mp3", 1000));
    rig.now += 4000;
    rig.player.tick();
    CHECK(rig.player.snapshot().state == PlayerState::Preparing);
    rig.now += 1500;
    rig.player.tick();
    CHECK(rig.player.snapshot().state == PlayerState::Error);
    CHECK(rig.backend.calls.back() == "stop");
    return 0;
}

int test_timeout_stop_failure_is_retried()
{
    Rig rig;
    CHECK(rig.player.play(0, "/slow.mp3", 1000));
    rig.backend.fail_stop = true;
    rig.now += 6000;
    rig.player.tick();
    CHECK(rig.player.snapshot().state == PlayerState::Error);

    rig.backend.fail_stop = false;
    rig.backend.calls.clear();
    rig.player.on_backend_state(BackendState::Playing);
    CHECK(rig.backend.calls.size() == 1 && rig.backend.calls[0] == "stop");
    CHECK(rig.player.snapshot().state == PlayerState::Error);
    rig.player.on_backend_state(BackendState::Playing);
    CHECK(rig.backend.calls.size() == 1);
    rig.player.on_backend_state(BackendState::Idle);
    rig.player.on_backend_state(BackendState::Playing);
    CHECK(rig.backend.calls.size() == 1);
    return 0;
}

int test_stop_failure_cleared_by_idle()
{
    Rig rig;
    CHECK(rig.player.play(0, "/slow.mp3", 1000));
    rig.backend.fail_stop = true;
    rig.now += 6000;
    rig.player.tick();
    rig.player.on_backend_state(BackendState::Idle);
    rig.backend.fail_stop = false;
    rig.backend.calls.clear();
    rig.player.on_backend_state(BackendState::Playing);
    CHECK(rig.backend.calls.empty());
    return 0;
}

int test_timeout_stop_failure_manual_stop()
{
    Rig rig;
    CHECK(rig.player.play(0, "/slow.mp3", 1000));
    rig.backend.fail_stop = true;
    rig.now += 6000;
    rig.player.tick();
    rig.backend.calls.clear();
    CHECK(!rig.player.stop());
    CHECK(rig.player.snapshot().state == PlayerState::Error);
    rig.backend.fail_stop = false;
    CHECK(rig.player.stop());
    CHECK(rig.backend.calls.size() == 2 && rig.backend.calls[1] == "stop");
    CHECK(rig.player.snapshot().state == PlayerState::Stopped);
    return 0;
}

int test_foreign_playback_is_left_alone()
{
    Rig rig;
    rig.start();
    CHECK(rig.player.stop());
    rig.player.on_backend_state(BackendState::Idle);
    rig.backend.calls.clear();
    rig.player.on_backend_state(BackendState::Playing);
    rig.player.on_backend_state(BackendState::Paused);
    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.backend.calls.empty());
    CHECK(rig.player.snapshot().state == PlayerState::Stopped);
    return 0;
}

int test_external_changes()
{
    Rig rig;
    rig.start();
    rig.now += 2000;
    rig.player.on_backend_state(BackendState::Paused);
    CHECK(rig.player.snapshot().state == PlayerState::Paused);
    rig.now += 3000;
    CHECK(rig.player.snapshot().elapsed_ms == 2000);
    rig.player.on_backend_state(BackendState::Playing);
    CHECK(rig.player.snapshot().state == PlayerState::Playing);

    rig.now += 1000;
    rig.player.on_backend_state(BackendState::Paused);
    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.player.snapshot().state == PlayerState::Stopped);
    CHECK(rig.player.snapshot().elapsed_ms == 0);
    CHECK(rig.ended.empty());
    return 0;
}

int test_synchronous_backend()
{
    Rig rig;
    rig.backend.sync_player = &rig.player;
    CHECK(rig.player.play(0, "/a.mp3", 5000));
    CHECK(rig.player.snapshot().state == PlayerState::Playing);
    CHECK(rig.player.play(1, "/b.mp3", 5000));
    CHECK(rig.player.snapshot().state == PlayerState::Playing);
    CHECK(rig.player.snapshot().index == 1);
    CHECK(rig.player.stop());
    CHECK(rig.player.snapshot().state == PlayerState::Stopped);
    return 0;
}

int test_ended_handler_may_start_next()
{
    Rig rig;
    rig.player.set_ended_handler([&rig](size_t index) {
        rig.ended.push_back(index);
        rig.player.play(index + 1, "/next.mp3", 4000);
    });
    rig.start(0, 3000);
    rig.now += 3000;
    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.player.snapshot().state == PlayerState::Preparing);
    CHECK(rig.player.snapshot().index == 1);
    rig.player.on_backend_state(BackendState::Playing);
    CHECK(rig.player.snapshot().state == PlayerState::Playing);
    return 0;
}

int test_version()
{
    Rig rig;
    uint32_t last = rig.player.snapshot().version;
    rig.player.play(0, "/a.mp3", 5000);
    CHECK(rig.player.snapshot().version > last);
    last = rig.player.snapshot().version;
    rig.player.on_backend_state(BackendState::Playing);
    CHECK(rig.player.snapshot().version > last);
    last = rig.player.snapshot().version;
    rig.now += 500;
    rig.player.tick();
    rig.player.on_backend_state(BackendState::Playing);
    CHECK(rig.player.snapshot().version == last);
    rig.player.pause();
    CHECK(rig.player.snapshot().version > last);
    return 0;
}

int test_threads()
{
    FakeBackend backend;
    std::atomic<uint64_t> now{1000};
    Player::Config config;
    config.now_ms = [&now] { return now.load(); };
    Player player(backend, config);
    std::atomic<bool> done{false};

    std::thread events([&] {
        for (int i = 0; i < 2000; i++) {
            player.on_backend_state(i % 3 == 0 ? BackendState::Idle : BackendState::Playing);
            now += 10;
        }
        done = true;
    });
    std::thread reader([&] {
        while (!done) {
            const PlayerSnapshot snap = player.snapshot();
            (void)snap;
            player.tick();
        }
    });
    for (int i = 0; i < 500; i++) {
        player.play(size_t(i), "/t.mp3", 2000);
        player.pause();
        player.resume();
        player.stop();
    }
    events.join();
    reader.join();
    return 0;
}

uint64_t last(Rig &rig, BackendCommand c) { return rig.backend.last[size_t(c)]; }

int test_async_play_failure()
{
    Rig rig;
    rig.player.play(0, "/a.mp3", 5000);
    rig.player.on_command_failed(BackendCommand::Play, last(rig, BackendCommand::Play));
    CHECK(rig.player.snapshot().state == PlayerState::Error);
    rig.player.on_backend_state(BackendState::Idle);
    CHECK(rig.player.snapshot().state == PlayerState::Error);
    CHECK(rig.player.play(1, "/b.mp3", 5000));
    CHECK(rig.player.snapshot().state == PlayerState::Preparing);
    return 0;
}

int test_async_pause_resume_failure()
{
    Rig rig;
    rig.start();
    rig.now += 1000;
    CHECK(rig.player.pause());
    rig.player.on_command_failed(BackendCommand::Pause, last(rig, BackendCommand::Pause));
    CHECK(rig.player.snapshot().state == PlayerState::Playing);
    rig.now += 500;
    CHECK(rig.player.snapshot().elapsed_ms == 1500);
    CHECK(rig.player.pause());
    CHECK(rig.player.resume());
    rig.player.on_command_failed(BackendCommand::Resume, last(rig, BackendCommand::Resume));
    CHECK(rig.player.snapshot().state == PlayerState::Paused);
    return 0;
}

int test_stale_failure_is_ignored()
{
    Rig rig;
    rig.start();
    CHECK(rig.player.pause());
    const uint64_t first_pause = last(rig, BackendCommand::Pause);
    CHECK(rig.player.resume());
    CHECK(rig.player.pause());
    rig.player.on_command_failed(BackendCommand::Pause, first_pause);
    CHECK(rig.player.snapshot().state == PlayerState::Paused);
    rig.player.on_command_failed(BackendCommand::Play, 1);
    CHECK(rig.player.snapshot().state == PlayerState::Paused);
    return 0;
}

int test_async_user_stop_failure_retries()
{
    Rig rig;
    rig.start();
    CHECK(rig.player.stop());
    CHECK(rig.player.snapshot().state == PlayerState::Stopped);
    rig.player.on_command_failed(BackendCommand::Stop, last(rig, BackendCommand::Stop));
    for (int i = 0; i < 3; i++) {
        rig.backend.calls.clear();
        rig.now += 500;
        rig.player.tick();
        CHECK(rig.backend.calls.size() == 1 && rig.backend.calls[0] == "stop");
        rig.player.on_command_failed(BackendCommand::Stop, last(rig, BackendCommand::Stop));
    }
    rig.backend.calls.clear();
    rig.now += 500;
    rig.player.tick();
    CHECK(rig.backend.calls.empty());
    CHECK(rig.player.snapshot().state == PlayerState::Error);
    return 0;
}

int test_async_stop_failure_after_idle_is_ignored()
{
    Rig rig;
    rig.start();
    CHECK(rig.player.stop());
    rig.player.on_backend_state(BackendState::Idle);
    rig.player.on_command_failed(BackendCommand::Stop, last(rig, BackendCommand::Stop));
    rig.backend.calls.clear();
    rig.now += 1000;
    rig.player.tick();
    CHECK(rig.backend.calls.empty());
    CHECK(rig.player.snapshot().state == PlayerState::Stopped);
    return 0;
}

int test_switch_stop_failure_restores_old_track()
{
    Rig rig;
    rig.start(0, 10000);
    rig.now += 2000;
    CHECK(rig.player.play(1, "/b.mp3", 5000));
    CHECK(rig.player.snapshot().state == PlayerState::Preparing);
    rig.now += 100;
    rig.player.on_command_failed(BackendCommand::Stop, last(rig, BackendCommand::Stop));
    PlayerSnapshot snap = rig.player.snapshot();
    CHECK(snap.state == PlayerState::Playing);
    CHECK(snap.index == 0);
    CHECK(snap.duration_ms == 10000);
    CHECK(snap.elapsed_ms == 2000);
    rig.now += 500;
    CHECK(rig.player.snapshot().elapsed_ms == 2500);
    rig.backend.calls.clear();
    rig.now += 5000;
    rig.player.tick();
    CHECK(rig.backend.calls.empty());
    return 0;
}

int test_switch_from_paused_stop_failure_restores_paused()
{
    Rig rig;
    rig.start();
    CHECK(rig.player.pause());
    rig.player.on_backend_state(BackendState::Paused);
    CHECK(rig.player.play(1, "/b.mp3", 5000));
    rig.player.on_command_failed(BackendCommand::Stop, last(rig, BackendCommand::Stop));
    const PlayerSnapshot snap = rig.player.snapshot();
    CHECK(snap.state == PlayerState::Paused);
    CHECK(snap.index == 0);
    return 0;
}

int test_confirmed_pause_ignores_failure()
{
    Rig rig;
    rig.start();
    CHECK(rig.player.pause());
    rig.player.on_backend_state(BackendState::Paused);
    rig.player.on_command_failed(BackendCommand::Pause, last(rig, BackendCommand::Pause));
    CHECK(rig.player.snapshot().state == PlayerState::Paused);
    return 0;
}

int test_confirmed_resume_ignores_failure()
{
    Rig rig;
    rig.start();
    CHECK(rig.player.pause());
    CHECK(rig.player.resume());
    rig.player.on_backend_state(BackendState::Playing);
    rig.player.on_command_failed(BackendCommand::Resume, last(rig, BackendCommand::Resume));
    CHECK(rig.player.snapshot().state == PlayerState::Playing);
    return 0;
}

int test_confirmed_play_ignores_failure()
{
    Rig rig;
    rig.start();
    rig.player.on_command_failed(BackendCommand::Play, last(rig, BackendCommand::Play));
    CHECK(rig.player.snapshot().state == PlayerState::Playing);
    return 0;
}

int test_synchronous_backend_keeps_sequences()
{
    Rig rig;
    rig.backend.sync_player = &rig.player;
    rig.player.play(0, "/a.mp3", 5000);
    CHECK(rig.player.snapshot().state == PlayerState::Playing);
    CHECK(rig.player.play(1, "/b.mp3", 5000));
    CHECK(rig.player.snapshot().state == PlayerState::Playing);
    CHECK(rig.player.snapshot().index == 1);
    rig.player.on_command_failed(BackendCommand::Stop, last(rig, BackendCommand::Stop));
    CHECK(rig.player.snapshot().state == PlayerState::Playing);
    CHECK(rig.player.pause());
    rig.player.on_command_failed(BackendCommand::Pause, last(rig, BackendCommand::Pause));
    CHECK(rig.player.snapshot().state == PlayerState::Playing);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    using TestFn = int (*)();
    const TestFn tests[] = {
        test_initial_state,
        test_play_and_elapsed,
        test_late_duration,
        test_late_duration_across_switch,
        test_pause_resume,
        test_pause_not_allowed_while_preparing,
        test_natural_end,
        test_short_playback_then_idle_is_ended,
        test_stop_ignores_idle,
        test_stop_failure_keeps_state,
        test_stop_while_paused_and_after_end,
        test_switch_track_while_playing,
        test_switch_while_preparing_cancels_first,
        test_switch_while_preparing_without_late_event,
        test_switch_while_preparing_stop_failure,
        test_stale_flag_does_not_outlive_preparing,
        test_rapid_switches_keep_last,
        test_switch_stop_failure_restores_state,
        test_switch_timeout_fails_without_starting,
        test_switch_timeout_then_retry_stops_again,
        test_stop_cancels_pending_switch,
        test_play_right_after_stop_waits_for_idle,
        test_natural_end_during_switch_request,
        test_switch_while_paused,
        test_play_failure,
        test_idle_while_preparing_is_error,
        test_prepare_timeout,
        test_timeout_stop_failure_is_retried,
        test_stop_failure_cleared_by_idle,
        test_timeout_stop_failure_manual_stop,
        test_foreign_playback_is_left_alone,
        test_external_changes,
        test_synchronous_backend,
        test_ended_handler_may_start_next,
        test_version,
        test_threads,
        test_async_play_failure,
        test_async_pause_resume_failure,
        test_stale_failure_is_ignored,
        test_async_user_stop_failure_retries,
        test_async_stop_failure_after_idle_is_ignored,
        test_switch_stop_failure_restores_old_track,
        test_switch_from_paused_stop_failure_restores_paused,
        test_confirmed_pause_ignores_failure,
        test_confirmed_resume_ignores_failure,
        test_confirmed_play_ignores_failure,
        test_synchronous_backend_keeps_sequences,
    };
    const int only = argc > 1 ? std::atoi(argv[1]) : -1;
    for (int i = 0; i < int(sizeof(tests) / sizeof(tests[0])); i++) {
        if (only >= 0 && only != i) continue;
        const int rc = tests[i]();
        if (rc != 0) {
            std::fprintf(stderr, "test %d failed\n", i);
            return rc;
        }
    }
    std::puts("music_player_test passed");
    return 0;
}
