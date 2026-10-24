#pragma once

namespace host_sim {

// Points the music data layer at a temporary folder with deterministic tracks.
// An existing ESP_CHATBOT_MUSIC_DIR is kept unless `force` is set.
void prepare_music_fixture(bool force);

} // namespace host_sim
