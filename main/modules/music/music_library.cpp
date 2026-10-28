#include "music_library.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unordered_set>

namespace music {
namespace {

struct Found {
    std::string path;
    std::string folder;
    std::string stem;
    std::string sort_key;
};

std::string lowercase(const std::string &s)
{
    std::string out = s;
    for (char &c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool has_mp3_extension(const char *name, size_t &stem_len)
{
    const size_t len = strlen(name);
    if (len <= 4) return false;
    const char *ext = name + len - 4;
    if (ext[0] != '.' || tolower(ext[1]) != 'm' || tolower(ext[2]) != 'p' || ext[3] != '3') return false;
    stem_len = len - 4;
    return true;
}

bool is_directory(const std::string &path, const dirent *entry)
{
    if (entry->d_type == DT_DIR) return true;
    if (entry->d_type != DT_UNKNOWN) return false;
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool is_regular(const std::string &path, const dirent *entry)
{
    if (entry->d_type == DT_REG) return true;
    if (entry->d_type != DT_UNKNOWN) return false;
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

struct ScanState {
    const ScanHooks &hooks;
    unsigned entries = 0;
    bool aborted = false;

    bool checkpoint()
    {
        if (hooks.checkpoint != nullptr && !hooks.checkpoint(hooks.ctx)) aborted = true;
        return !aborted;
    }
};

void scan_folder(const std::string &dir, const std::string &folder, bool recurse, std::vector<Found> &out,
                 ScanState &state)
{
    if (!state.checkpoint()) return;
    DIR *handle = opendir(dir.c_str());
    if (handle == nullptr) return;
    while (const dirent *entry = readdir(handle)) {
        if (out.size() >= Library::kMaxTracks) break;
        if ((++state.entries % 16) == 0 && !state.checkpoint()) break;
        const char *name = entry->d_name;
        if (name[0] == '.') continue;
        const std::string full = dir + "/" + name;
        if (is_directory(full, entry)) {
            if (recurse) scan_folder(full, name, false, out, state);
            continue;
        }
        size_t stem_len = 0;
        if (!has_mp3_extension(name, stem_len) || !is_regular(full, entry)) continue;
        Found found;
        found.path = full;
        found.folder = folder;
        found.stem.assign(name, stem_len);
        found.sort_key = lowercase(folder.empty() ? found.stem : folder + "/" + found.stem);
        out.push_back(std::move(found));
    }
    closedir(handle);
}

} // namespace

size_t Library::scan()
{
    std::vector<Track> next;
    scan_into(next);
    tracks_ = std::move(next);
    return tracks_.size();
}

bool Library::scan_into(std::vector<Track> &out, const ScanHooks &hooks) const
{
    std::vector<Found> found;
    ScanState state{hooks};
    scan_folder(root_, "", true, found, state);
    if (state.aborted) return false;
    std::sort(found.begin(), found.end(), [](const Found &a, const Found &b) { return a.sort_key < b.sort_key; });

    out.clear();
    out.reserve(found.size());
    std::unordered_set<std::string> used;
    for (Found &f : found) {
        Track track;
        track.path = std::move(f.path);
        track.display_name = f.folder.empty() ? f.stem : f.folder + "_" + f.stem;
        const std::string base = track.display_name;
        for (int n = 2; used.count(lowercase(track.display_name)) != 0; n++) {
            track.display_name = base + " #" + std::to_string(n);
        }
        used.insert(lowercase(track.display_name));
        out.push_back(std::move(track));
    }
    return true;
}

void Library::apply_meta(size_t index, const TrackMeta &meta)
{
    if (index >= tracks_.size()) return;
    tracks_[index].meta = meta;
    tracks_[index].state = MetaState::Ready;
}

void Library::mark_failed(size_t index)
{
    if (index < tracks_.size()) tracks_[index].state = MetaState::Failed;
}

ParseResult Library::load_meta(size_t index, const ParseOptions &options)
{
    if (index >= tracks_.size()) return ParseResult::IoError;
    Track &track = tracks_[index];
    TrackMeta meta;
    const ParseResult result = parse_mp3_file(track.path.c_str(), meta, options);
    if (result == ParseResult::Cancelled) return result;
    if (result == ParseResult::Ok) {
        track.meta = meta;
        track.state = MetaState::Ready;
    } else {
        track.state = MetaState::Failed;
    }
    return result;
}

const char *Library::title_of(const Track &track)
{
    if (track.state == MetaState::Ready && track.meta.title[0] != '\0') return track.meta.title;
    return track.display_name.c_str();
}

} // namespace music
