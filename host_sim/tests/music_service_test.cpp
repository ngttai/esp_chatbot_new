#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "modules/music/music_service.hpp"
#include "mp3_fixtures.hpp"

using namespace mp3fixture;
using namespace std::chrono_literals;

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition); \
    return 1; } } while (false)

namespace {

template <class Predicate>
bool await(Predicate predicate, std::chrono::milliseconds timeout = 3000ms)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(5ms);
    }
    return predicate();
}

void make_library(const fs::path &dir, int tracks)
{
    for (int i = 1; i <= tracks; i++) {
        char name[32];
        std::snprintf(name, sizeof(name), "t%02d", i);
        Bytes tag;
        append(tag, text_frame(3, "TIT2", 0, text(std::string("Title ") + name)));
        append(tag, text_frame(3, "TPE1", 0, text(std::string("Artist ") + name)));
        Bytes file = id3v2(3, tag);
        append(file, mpeg_frames(20));
        write_file(dir / (std::string(name) + ".mp3"), file);
    }
}

// Counts parse calls per file and can stall to exercise cancellation.
struct ParseProbe {
    std::mutex mutex;
    std::map<std::string, int> calls;
    std::map<std::string, int> cancelled;
    std::vector<std::string> order;
    std::atomic<bool> stall{false};
    std::atomic<int> stalled_now{0};

    music::Service::ParseFn fn()
    {
        return [this](const char *path, music::TrackMeta &meta, const music::ParseOptions &options) {
            std::string name = fs::path(path).filename().string();
            {
                std::lock_guard<std::mutex> lock(mutex);
                calls[name]++;
                order.push_back(name);
            }
            if (stall.load()) {
                stalled_now++;
                const auto deadline = std::chrono::steady_clock::now() + 3000ms;
                while (std::chrono::steady_clock::now() < deadline) {
                    if (options.cancelled(options.ctx)) {
                        std::lock_guard<std::mutex> lock(mutex);
                        cancelled[name]++;
                        stalled_now--;
                        return music::ParseResult::Cancelled;
                    }
                    std::this_thread::sleep_for(2ms);
                }
                stalled_now--;
            }
            return music::parse_mp3_file(path, meta, options);
        };
    }

    int count(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = calls.find(name);
        return it == calls.end() ? 0 : it->second;
    }

    std::vector<std::string> parse_order()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return order;
    }

    int cancel_count(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = cancelled.find(name);
        return it == cancelled.end() ? 0 : it->second;
    }

    int total()
    {
        std::lock_guard<std::mutex> lock(mutex);
        int sum = 0;
        for (auto &kv : calls) sum += kv.second;
        return sum;
    }
};

bool ready(music::Service &service, size_t index)
{
    music::TrackView view;
    return service.snapshot(index, view) && view.state == music::MetaState::Ready;
}

music::Service::Config make_config(const fs::path &dir, ParseProbe &probe)
{
    music::Service::Config config;
    config.root = dir.string();
    config.settle_ms = 30;
    config.parse = probe.fn();
    return config;
}

int test_scan_and_first_track(const fs::path &root)
{
    const fs::path dir = root / "first";
    make_library(dir, 12);
    ParseProbe probe;
    music::Service service(make_config(dir, probe));
    CHECK(service.count() == 0);
    service.start();

    CHECK(await([&] { return service.scan_done(); }));
    CHECK(service.count() == 12);
    CHECK(await([&] { return ready(service, 0); }));

    music::TrackView view;
    CHECK(service.snapshot(0, view));
    CHECK(std::string(view.title) == "Title t01");
    CHECK(std::string(view.artist) == "Artist t01");
    CHECK(view.duration_ms > 0);

    CHECK(service.snapshot(5, view));
    CHECK(view.state == music::MetaState::Pending);
    CHECK(std::string(view.title) == "t06");
    CHECK(!service.snapshot(12, view));

    std::this_thread::sleep_for(150ms);
    CHECK(probe.total() == 1);
    return 0;
}

int test_wanted_loads_visible_rows(const fs::path &root)
{
    const fs::path dir = root / "wanted";
    make_library(dir, 12);
    ParseProbe probe;
    music::Service service(make_config(dir, probe));
    service.start();
    CHECK(await([&] { return ready(service, 0); }));

    const uint32_t before = service.version();
    const size_t rows[] = {1, 2, 3, 4};
    service.set_wanted(rows, 4);
    CHECK(await([&] { return ready(service, 1) && ready(service, 2) && ready(service, 3) && ready(service, 4); }));
    CHECK(service.version() >= before + 4);
    CHECK(!ready(service, 5));

    music::TrackView view;
    CHECK(service.snapshot(4, view));
    CHECK(std::string(view.title) == "Title t05");

    // Scrolling by one row only loads the new row.
    const int calls_before = probe.total();
    const size_t shifted[] = {2, 3, 4, 5};
    service.set_wanted(shifted, 4);
    CHECK(await([&] { return ready(service, 5); }));
    std::this_thread::sleep_for(150ms);
    CHECK(probe.total() == calls_before + 1);

    // Re-requesting loaded rows never parses again.
    service.set_wanted(rows, 4);
    std::this_thread::sleep_for(150ms);
    CHECK(probe.total() == calls_before + 1);
    return 0;
}

int test_ui_busy_blocks_loading(const fs::path &root)
{
    const fs::path dir = root / "busy";
    make_library(dir, 8);
    ParseProbe probe;
    music::Service service(make_config(dir, probe));
    service.start();
    CHECK(await([&] { return ready(service, 0); }));

    service.set_ui_idle(false);
    const size_t rows[] = {1, 2, 3};
    service.set_wanted(rows, 3);
    std::this_thread::sleep_for(300ms);
    CHECK(!ready(service, 1));
    CHECK(probe.total() == 1);

    service.set_ui_idle(true);
    CHECK(await([&] { return ready(service, 1) && ready(service, 2) && ready(service, 3); }));
    return 0;
}

int test_scroll_cancels_in_flight_parse(const fs::path &root)
{
    const fs::path dir = root / "cancel";
    make_library(dir, 8);
    ParseProbe probe;
    music::Service service(make_config(dir, probe));
    service.start();
    CHECK(await([&] { return ready(service, 0); }));

    probe.stall = true;
    const size_t rows[] = {3};
    service.set_wanted(rows, 1);
    CHECK(await([&] { return probe.stalled_now.load() == 1; }));

    const auto begin = std::chrono::steady_clock::now();
    service.set_ui_idle(false);
    CHECK(await([&] { return probe.stalled_now.load() == 0; }, 500ms));
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    CHECK(elapsed < 300ms);
    CHECK(!ready(service, 3));

    probe.stall = false;
    service.set_ui_idle(true);
    CHECK(await([&] { return ready(service, 3); }));
    return 0;
}

int test_wanted_change_cancels_stale_parse(const fs::path &root)
{
    const fs::path dir = root / "stale";
    make_library(dir, 8);
    ParseProbe probe;
    music::Service service(make_config(dir, probe));
    service.start();
    CHECK(await([&] { return ready(service, 0); }));

    probe.stall = true;
    const size_t first[] = {2};
    service.set_wanted(first, 1);
    CHECK(await([&] { return probe.stalled_now.load() == 1; }));

    probe.stall = false;
    const size_t second[] = {5};
    service.set_wanted(second, 1);
    CHECK(await([&] { return ready(service, 5); }));
    CHECK(probe.cancel_count("t03.mp3") == 1);
    std::this_thread::sleep_for(100ms);
    CHECK(!ready(service, 2));
    CHECK(probe.count("t03.mp3") == 1);
    return 0;
}

int test_pinned_goes_first(const fs::path &root)
{
    const fs::path dir = root / "pinned";
    make_library(dir, 10);
    ParseProbe probe;
    music::Service service(make_config(dir, probe));
    service.set_ui_idle(false);
    service.set_pinned(7);
    const size_t rows[] = {1, 2};
    service.set_wanted(rows, 2);
    service.start();
    std::this_thread::sleep_for(150ms);
    CHECK(!service.scan_done());
    CHECK(probe.total() == 0);

    service.set_ui_idle(true);
    CHECK(await([&] { return ready(service, 7) && ready(service, 1) && ready(service, 2); }));
    const std::vector<std::string> expected{"t08.mp3", "t02.mp3", "t03.mp3"};
    CHECK(probe.parse_order() == expected);
    return 0;
}

int test_wanted_cap_and_bad_indices(const fs::path &root)
{
    const fs::path dir = root / "cap";
    make_library(dir, 20);
    ParseProbe probe;
    music::Service service(make_config(dir, probe));
    service.start();
    CHECK(await([&] { return ready(service, 0); }));

    size_t many[20];
    for (size_t i = 0; i < 20; i++) many[i] = i + 1;
    service.set_wanted(many, 20);
    CHECK(await([&] { return ready(service, 8); }));
    std::this_thread::sleep_for(150ms);
    CHECK(!ready(service, 9));
    CHECK(probe.total() == 9);

    const size_t bad[] = {500, 9999};
    service.set_wanted(bad, 2);
    service.set_pinned(12345);
    std::this_thread::sleep_for(100ms);
    CHECK(probe.total() == 9);
    return 0;
}

int test_failed_file_is_not_retried(const fs::path &root)
{
    const fs::path dir = root / "failed";
    make_library(dir, 6);
    ParseProbe probe;
    music::Service service(make_config(dir, probe));
    service.start();
    CHECK(await([&] { return service.scan_done(); }));
    CHECK(await([&] { return ready(service, 0); }));

    fs::remove(dir / "t03.mp3");
    const size_t rows[] = {2};
    service.set_wanted(rows, 1);
    music::TrackView view;
    CHECK(await([&] { return service.snapshot(2, view) && view.state == music::MetaState::Failed; }));
    std::this_thread::sleep_for(150ms);
    CHECK(probe.count("t03.mp3") == 1);
    return 0;
}

int test_stop_while_blocked(const fs::path &root)
{
    const fs::path dir = root / "stop";
    make_library(dir, 40);
    ParseProbe probe;
    const auto begin = std::chrono::steady_clock::now();
    {
        music::Service service(make_config(dir, probe));
        service.set_ui_idle(false);
        service.start();
        std::this_thread::sleep_for(100ms);
        CHECK(!service.scan_done());
    }
    CHECK(std::chrono::steady_clock::now() - begin < 1000ms);

    {
        music::Service service(make_config(dir, probe));
        service.start();
        CHECK(await([&] { return ready(service, 0); }));
        probe.stall = true;
        const size_t rows[] = {4};
        service.set_wanted(rows, 1);
        CHECK(await([&] { return probe.stalled_now.load() == 1; }));
    }
    CHECK(probe.stalled_now.load() == 0);
    return 0;
}

int test_empty_and_missing_root(const fs::path &root)
{
    ParseProbe probe;
    music::Service service(make_config(root / "does-not-exist", probe));
    service.start();
    CHECK(await([&] { return service.scan_done(); }));
    CHECK(service.count() == 0);
    music::TrackView view;
    CHECK(!service.snapshot(0, view));
    service.set_pinned(0);
    std::this_thread::sleep_for(100ms);
    CHECK(probe.total() == 0);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    char pattern[] = "/tmp/music_service_test_XXXXXX";
    char *base = mkdtemp(pattern);
    if (base == nullptr) return 2;
    const fs::path root(base);

    using TestFn = int (*)(const fs::path &);
    const TestFn tests[] = {
        test_scan_and_first_track,
        test_wanted_loads_visible_rows,
        test_ui_busy_blocks_loading,
        test_scroll_cancels_in_flight_parse,
        test_wanted_change_cancels_stale_parse,
        test_pinned_goes_first,
        test_wanted_cap_and_bad_indices,
        test_failed_file_is_not_retried,
        test_stop_while_blocked,
        test_empty_and_missing_root,
    };
    const int only = argc > 1 ? std::atoi(argv[1]) : -1;

    int rc = 0;
    for (int i = 0; i < int(sizeof(tests) / sizeof(tests[0])) && rc == 0; i++) {
        if (only >= 0 && only != i) continue;
        rc = tests[i](root);
        if (rc != 0) std::fprintf(stderr, "test %d failed\n", i);
    }

    std::error_code ec;
    fs::remove_all(root, ec);
    if (rc == 0) std::puts("music_service_test passed");
    return rc;
}
