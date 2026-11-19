#pragma once

#include <functional>
#include <memory>

#include "music_player.hpp"

namespace music {

// Backend on top of the Audio Playback service. Commands run on a worker
// thread, so the caller never waits for the service. `on_state` is called from
// the service threads for every play state change and `on_failed` from the
// worker when the service refuses a command.
std::unique_ptr<PlayerBackend> make_audio_backend(std::function<void(BackendState)> on_state,
                                                  std::function<void(BackendCommand, uint64_t)> on_failed);

} // namespace music
