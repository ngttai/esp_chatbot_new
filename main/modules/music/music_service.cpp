#include "music_service.hpp"

#include <algorithm>
#include <cstring>

namespace music {
namespace {

void copy_truncated(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && (static_cast<unsigned char>(src[n]) & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

} // namespace

Service::Service(Config config): config_(std::move(config)), library_(config_.root)
{
    if (!config_.parse) {
        config_.parse = [](const char *path, TrackMeta &meta, const ParseOptions &options) {
            return parse_mp3_file(path, meta, options);
        };
    }
}

void Service::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_.load()) return;
    running_ = true;
    if (config_.thread_setup) config_.thread_setup();
    thread_ = std::thread([this] { run(); });
}

void Service::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void Service::set_ui_idle(bool idle)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (idle == !busy_.load()) return;
        busy_ = !idle;
        if (idle) idle_since_ = std::chrono::steady_clock::now();
    }
    cv_.notify_all();
}

void Service::set_pinned(size_t index)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pinned_ == index) return;
        pinned_ = index;
        epoch_++;
    }
    cv_.notify_all();
}

void Service::set_wanted(const size_t *indices, size_t count)
{
    if (indices == nullptr) count = 0;
    count = std::min(count, kMaxWanted);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool unchanged = count == wanted_count_ &&
            (count == 0 || std::equal(indices, indices + count, wanted_));
        if (unchanged) return;
        if (count > 0) std::copy(indices, indices + count, wanted_);
        wanted_count_ = count;
        epoch_++;
    }
    cv_.notify_all();
}

size_t Service::count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return library_.count();
}

bool Service::snapshot(size_t index, TrackView &out) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const Track *track = library_.track(index);
    if (track == nullptr) return false;
    memset(&out, 0, sizeof(out));
    out.state = track->state;
    copy_truncated(out.title, sizeof(out.title), Library::title_of(*track));
    if (track->state == MetaState::Ready) {
        copy_truncated(out.artist, sizeof(out.artist), track->meta.artist);
        copy_truncated(out.album, sizeof(out.album), track->meta.album);
        out.duration_ms = track->meta.duration_ms;
    }
    return true;
}

bool Service::settled_locked() const
{
    if (busy_.load()) return false;
    return std::chrono::steady_clock::now() >= idle_since_ + std::chrono::milliseconds(config_.settle_ms);
}

bool Service::wait_until_settled(std::unique_lock<std::mutex> &lock)
{
    while (running_.load()) {
        if (busy_.load()) {
            cv_.wait(lock);
        } else if (!settled_locked()) {
            cv_.wait_until(lock, idle_since_ + std::chrono::milliseconds(config_.settle_ms));
        } else {
            return true;
        }
    }
    return false;
}

bool Service::scan_checkpoint(void *ctx)
{
    auto *self = static_cast<Service *>(ctx);
    std::unique_lock<std::mutex> lock(self->mutex_);
    return self->wait_until_settled(lock);
}

bool Service::parse_cancelled(void *ctx)
{
    auto *c = static_cast<CancelContext *>(ctx);
    return !c->service->running_.load() || c->service->busy_.load() || c->service->epoch_.load() != c->epoch;
}

bool Service::scan_phase()
{
    std::vector<Track> found;
    ScanHooks hooks{&Service::scan_checkpoint, this};
    if (!library_.scan_into(found, hooks)) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        library_.adopt(std::move(found));
        epoch_++;
    }
    scan_done_ = true;
    version_++;
    return true;
}

bool Service::pick_next_locked(size_t &index) const
{
    auto pending = [this](size_t i) {
        const Track *t = library_.track(i);
        return t != nullptr && t->state == MetaState::Pending;
    };
    if (pending(pinned_)) {
        index = pinned_;
        return true;
    }
    for (size_t i = 0; i < wanted_count_; i++) {
        if (pending(wanted_[i])) {
            index = wanted_[i];
            return true;
        }
    }
    return false;
}

void Service::run()
{
    if (!scan_phase()) return;

    std::unique_lock<std::mutex> lock(mutex_);
    while (running_.load()) {
        if (!wait_until_settled(lock)) break;

        size_t index = 0;
        if (!pick_next_locked(index)) {
            cv_.wait(lock);
            continue;
        }

        const std::string path = library_.track(index)->path;
        CancelContext cancel{this, epoch_.load()};
        const ParseOptions options{&Service::parse_cancelled, &cancel};
        lock.unlock();
        TrackMeta meta;
        const ParseResult result = config_.parse(path.c_str(), meta, options);
        lock.lock();

        if (result == ParseResult::Cancelled) continue;
        const Track *track = library_.track(index);
        if (track == nullptr || track->path != path) continue;
        if (result == ParseResult::Ok) {
            library_.apply_meta(index, meta);
        } else {
            library_.mark_failed(index);
        }
        version_++;
    }
}

} // namespace music
