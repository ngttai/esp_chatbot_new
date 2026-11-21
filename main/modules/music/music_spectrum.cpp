#include "music_spectrum.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <new>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace {
constexpr unsigned kSamples = 512;
constexpr float kPi = 3.14159265358979323846f;
struct Spectrum {
    std::mutex mutex;
    char uri[520]{};
    int16_t samples[kSamples]{};
    float window[kSamples]{};
    float real[kSamples]{};
    float imag[kSamples]{};
    float twiddle_real[kSamples / 2]{};
    float twiddle_imag[kSamples / 2]{};
    unsigned write = 0, count = 0, pending = 0, rate = 0;
    Spectrum()
    {
        for (unsigned i = 0; i < kSamples; ++i) window[i] = 0.5f - 0.5f * cosf(2 * kPi * i / (kSamples - 1));
        for (unsigned i = 0; i < kSamples / 2; ++i) {
            twiddle_real[i] = cosf(2 * kPi * i / kSamples);
            twiddle_imag[i] = -sinf(2 * kPi * i / kSamples);
        }
    }
};
// Published once and retained: the audio callback can never observe freed storage.
std::atomic<Spectrum *> state{nullptr};
std::atomic<bool> enabled{false};
Spectrum *ensure_state()
{
    auto *s = state.load(std::memory_order_acquire);
    if (s) return s;
#ifdef ESP_PLATFORM
    void *memory = heap_caps_malloc(sizeof(Spectrum), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void *memory = ::operator new(sizeof(Spectrum), std::nothrow);
#endif
    if (!memory) return nullptr;
    s = new(memory) Spectrum;
    state.store(s, std::memory_order_release);
    return s;
}
int32_t sample(const uint8_t *data, unsigned bits)
{
    if (bits == 16) return int16_t(uint16_t(data[0]) | uint16_t(data[1]) << 8);
    if (bits == 24) return int16_t(uint16_t(data[1]) | uint16_t(data[2]) << 8);
    return int16_t(uint16_t(data[2]) | uint16_t(data[3]) << 8);
}
}
extern "C" {
bool music_spectrum_set_enabled(bool value)
{
    enabled.store(false, std::memory_order_release);
    auto *s = value ? ensure_state() : state.load(std::memory_order_acquire);
    if (!s) return !value;
    std::lock_guard<std::mutex> lock(s->mutex);
    s->write = s->count = s->pending = 0;
    enabled.store(value, std::memory_order_release);
    return true;
}
bool music_spectrum_is_enabled(void) { return enabled.load(std::memory_order_acquire); }
void music_spectrum_set_track(const char *path)
{
    auto *s = ensure_state();
    if (!s) return;
    std::lock_guard<std::mutex> lock(s->mutex);
    s->write = s->count = s->pending = 0;
    s->uri[0] = 0;
    if (!path) return;
    const char *relative = path[0] == '/' ? path + 1 : path;
    if (strlen(relative) + 8 > sizeof(s->uri)) return;
    memcpy(s->uri, "file://", 7);
    strcpy(s->uri + 7, relative);
}
void music_spectrum_feed(const char *uri, const uint8_t *data, size_t bytes,
                         unsigned rate, unsigned channels, unsigned bits)
{
    if (!enabled.load(std::memory_order_acquire) || !uri || !data || !rate ||
        !channels || channels > 2 || (bits != 16 && bits != 24 && bits != 32)) return;
    auto *s = state.load(std::memory_order_acquire);
    if (!s) return;
    std::unique_lock<std::mutex> lock(s->mutex, std::try_to_lock);
    if (!lock.owns_lock() || !enabled.load(std::memory_order_acquire) || !s->uri[0] || strcmp(uri, s->uri)) return;
    if (s->rate != rate) { s->write = s->count = s->pending = 0; s->rate = rate; }
    const unsigned stride = channels * (bits / 8);
    const size_t frames = bytes / stride;
    // Copy at most one FFT window; discard older data when a callback is larger.
    const size_t first = frames > kSamples ? frames - kSamples : 0;
    for (size_t i = first; i < frames; ++i) {
        const uint8_t *frame = data + i * stride;
        int32_t mono = sample(frame, bits);
        if (channels == 2) mono = (mono + sample(frame + bits / 8, bits)) / 2;
        s->samples[s->write] = int16_t(mono);
        s->write = (s->write + 1) % kSamples;
        s->count = std::min(s->count + 1, kSamples);
    }
    s->pending += unsigned(frames - first);
    s->pending = std::min(s->pending, kSamples);
}
bool music_spectrum_read(uint8_t levels[MUSIC_SPECTRUM_BANDS])
{
    if (!levels || !enabled.load(std::memory_order_acquire)) return false;
    auto *s = state.load(std::memory_order_acquire);
    if (!s) return false;
    unsigned rate;
    {
        std::unique_lock<std::mutex> lock(s->mutex, std::try_to_lock);
        if (!lock.owns_lock() || s->count < kSamples || s->pending < kSamples / 2) return false;
        rate = s->rate;
        float mean = 0;
        for (unsigned i = 0; i < kSamples; ++i) mean += s->samples[i];
        mean /= kSamples;
        for (unsigned i = 0; i < kSamples; ++i) {
            s->real[i] = (s->samples[(s->write + i) % kSamples] - mean) * s->window[i] / 32768.f;
            s->imag[i] = 0;
        }
        s->pending = 0;
    }
    for (unsigned i = 1, j = 0; i < kSamples; ++i) {
        unsigned bit = kSamples >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(s->real[i], s->real[j]); std::swap(s->imag[i], s->imag[j]); }
    }
    for (unsigned length = 2; length <= kSamples; length <<= 1) {
        for (unsigned start = 0; start < kSamples; start += length) {
            for (unsigned i = 0; i < length / 2; ++i) {
                const unsigned a = start + i, b = a + length / 2, t = i * kSamples / length;
                const float r = s->real[b] * s->twiddle_real[t] - s->imag[b] * s->twiddle_imag[t];
                const float im = s->real[b] * s->twiddle_imag[t] + s->imag[b] * s->twiddle_real[t];
                s->real[b] = s->real[a] - r; s->imag[b] = s->imag[a] - im;
                s->real[a] += r; s->imag[a] += im;
            }
        }
    }
    // Logarithmic bands from 62.5 Hz to 8 kHz; absolute dBFS, no fake motion.
    static constexpr unsigned edges[] = {62,85,115,156,211,286,388,526,713,966,1310,1776,2406,3260,4417,5984,8001};
    for (unsigned band = 0; band < MUSIC_SPECTRUM_BANDS; ++band) {
        float peak = 0;
        const unsigned low = std::max(1u, (edges[band] * kSamples + rate - 1) / rate);
        const unsigned high = std::min(kSamples / 2, std::max(low + 1, (edges[band + 1] * kSamples + rate - 1) / rate));
        for (unsigned bin = low; bin < high; ++bin) peak = std::max(peak, s->real[bin] * s->real[bin] + s->imag[bin] * s->imag[bin]);
        const float db = 10.f * log10f(std::max(peak * 16.f / (kSamples * kSamples), 1e-12f));
        levels[band] = uint8_t(std::clamp((db + 60.f) * (100.f / 60.f), 0.f, 100.f));
    }
    return true;
}
#ifdef ESP_PLATFORM
// Strong implementation of the optional simple-player tap.
void esp_audio_simple_player_pcm_observer(const char *uri, const uint8_t *data, size_t bytes,
                                          unsigned rate, unsigned channels, unsigned bits)
{
    music_spectrum_feed(uri, data, bytes, rate, channels, bits);
}
#endif
}
