#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "modules/music/id3_parser.hpp"
#include "modules/music/music_library.hpp"
#include "mp3_fixtures.hpp"

namespace fs = std::filesystem;
using namespace mp3fixture;

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition); \
    return 1; } } while (false)

namespace {

music::ParseResult parse(const fs::path &path, music::TrackMeta &meta, const music::ParseOptions &options = {})
{
    return music::parse_mp3_file(path.string().c_str(), meta, options);
}

struct CallCounter {
    int calls = 0;
    int cancel_after = -1;
};

bool counter_cancelled(void *ctx)
{
    auto *c = static_cast<CallCounter *>(ctx);
    c->calls++;
    return c->cancel_after >= 0 && c->calls > c->cancel_after;
}

int test_id3v23_latin1(const fs::path &dir)
{
    Bytes tag;
    append(tag, text_frame(3, "TIT2", 0, text("Morning Drive")));
    append(tag, text_frame(3, "TPE1", 0, text("Aurora Bay")));
    append(tag, text_frame(3, "TALB", 0, text("Sunrise")));
    Bytes file = id3v2(3, tag, 64);
    append(file, mpeg_frames(100));
    write_file(dir / "a.mp3", file);

    music::TrackMeta meta;
    CHECK(parse(dir / "a.mp3", meta) == music::ParseResult::Ok);
    CHECK(meta.has_tag);
    CHECK(std::string(meta.title) == "Morning Drive");
    CHECK(std::string(meta.artist) == "Aurora Bay");
    CHECK(std::string(meta.album) == "Sunrise");
    CHECK(meta.duration_ms == 2606);
    return 0;
}

int test_id3v24_encodings(const fs::path &dir)
{
    Bytes tag;
    append(tag, text_frame(4, "TIT2", 3, text("Gạt Đi Nước Mắt")));
    Bytes artists = utf16_le(u"Noo");
    artists.push_back(0);
    artists.push_back(0);
    append(artists, utf16_le(u"DJ SlimV"));
    append(tag, text_frame(4, "TPE1", 1, artists));
    append(tag, text_frame(4, "TALB", 0, text("Caf\xE9")));
    Bytes file = id3v2(4, tag);
    append(file, mpeg_frames(50));
    write_file(dir / "b.mp3", file);

    music::TrackMeta meta;
    CHECK(parse(dir / "b.mp3", meta) == music::ParseResult::Ok);
    CHECK(std::string(meta.title) == "Gạt Đi Nước Mắt");
    CHECK(std::string(meta.artist) == "Noo, DJ SlimV");
    CHECK(std::string(meta.album) == "Caf\xC3\xA9");

    Bytes tag2;
    Bytes be{0xFE, 0xFF, 0x00, 'H', 0x00, 'i'};
    append(tag2, text_frame(4, "TIT2", 1, be));
    append(tag2, text_frame(4, "TPE1", 2, Bytes{0x00, 'A', 0x00, 'B'}));
    Bytes file2 = id3v2(4, tag2);
    append(file2, mpeg_frames(10));
    write_file(dir / "b2.mp3", file2);
    CHECK(parse(dir / "b2.mp3", meta) == music::ParseResult::Ok);
    CHECK(std::string(meta.title) == "Hi");
    CHECK(std::string(meta.artist) == "AB");
    return 0;
}

int test_unsynchronisation(const fs::path &dir)
{
    Bytes payload{1, 0xFF, 0x00, 0xFE, 'H', 0x00, 'i', 0x00};
    Bytes tag = frame(3, "TIT2", payload);
    Bytes file = id3v2(3, tag, 0, 0x80);
    append(file, mpeg_frames(10));
    write_file(dir / "unsync.mp3", file);

    music::TrackMeta meta;
    CHECK(parse(dir / "unsync.mp3", meta) == music::ParseResult::Ok);
    CHECK(std::string(meta.title) == "Hi");
    return 0;
}

int test_tlen_and_xing(const fs::path &dir)
{
    Bytes tag = text_frame(3, "TLEN", 0, text("180000"));
    Bytes file = id3v2(3, tag);
    append(file, mpeg_frames(20));
    write_file(dir / "tlen.mp3", file);

    music::TrackMeta meta;
    CHECK(parse(dir / "tlen.mp3", meta) == music::ParseResult::Ok);
    CHECK(meta.duration_ms == 180000);

    Bytes xing = id3v2(3, text_frame(3, "TIT2", 0, text("Vbr")));
    append(xing, xing_frames(1000, 5));
    write_file(dir / "xing.mp3", xing);
    CHECK(parse(dir / "xing.mp3", meta) == music::ParseResult::Ok);
    CHECK(meta.duration_ms == uint32_t(uint64_t(1000) * 1152 * 1000 / 44100));

    Bytes vbri = mpeg_frames(5);
    memcpy(vbri.data() + 4 + 32, "VBRI", 4);
    const Bytes vbri_frames = be32(500);
    memcpy(vbri.data() + 4 + 32 + 14, vbri_frames.data(), 4);
    write_file(dir / "vbri.mp3", vbri);
    CHECK(parse(dir / "vbri.mp3", meta) == music::ParseResult::Ok);
    CHECK(meta.duration_ms == uint32_t(uint64_t(500) * 1152 * 1000 / 44100));
    return 0;
}

int test_large_cover_is_skipped(const fs::path &dir)
{
    Bytes cover(300 * 1024, 0x55);
    Bytes apic{0};
    append(apic, text("image/jpeg"));
    apic.push_back(0);
    apic.push_back(3);
    apic.push_back(0);
    append(apic, cover);
    Bytes tag = frame(3, "APIC", apic);
    append(tag, text_frame(3, "TIT2", 0, text("After Cover")));
    append(tag, text_frame(3, "TPE1", 0, text("Band")));
    Bytes file = id3v2(3, tag);
    append(file, mpeg_frames(30));
    write_file(dir / "cover.mp3", file);

    music::TrackMeta meta;
    CallCounter counter;
    music::ParseOptions options{counter_cancelled, &counter};
    CHECK(parse(dir / "cover.mp3", meta, options) == music::ParseResult::Ok);
    CHECK(std::string(meta.title) == "After Cover");
    CHECK(std::string(meta.artist) == "Band");
    CHECK(counter.calls < 24);
    return 0;
}

int test_id3v1_and_fallbacks(const fs::path &dir)
{
    Bytes only_v1 = mpeg_frames(40);
    append(only_v1, id3v1("Old Song", "Old Artist", "Old Album"));
    write_file(dir / "v1.mp3", only_v1);

    music::TrackMeta meta;
    CHECK(parse(dir / "v1.mp3", meta) == music::ParseResult::Ok);
    CHECK(meta.has_tag);
    CHECK(std::string(meta.title) == "Old Song");
    CHECK(std::string(meta.artist) == "Old Artist");
    CHECK(meta.duration_ms == uint32_t(uint64_t(417) * 40 * 8000 / 128000));

    Bytes mixed = id3v2(3, text_frame(3, "TPE1", 0, text("V2 Artist")), 16);
    append(mixed, mpeg_frames(5));
    append(mixed, id3v1("V1 Title", "V1 Artist", ""));
    write_file(dir / "mixed.mp3", mixed);
    CHECK(parse(dir / "mixed.mp3", meta) == music::ParseResult::Ok);
    CHECK(std::string(meta.title) == "V1 Title");
    CHECK(std::string(meta.artist) == "V2 Artist");

    write_file(dir / "plain.mp3", mpeg_frames(12));
    CHECK(parse(dir / "plain.mp3", meta) == music::ParseResult::Ok);
    CHECK(!meta.has_tag);
    CHECK(meta.title[0] == '\0');
    CHECK(meta.duration_ms == uint32_t(uint64_t(417) * 12 * 8000 / 128000));
    return 0;
}

int test_corrupt_inputs(const fs::path &dir)
{
    music::TrackMeta meta;

    Bytes truncated = id3v2(3, text_frame(3, "TIT2", 0, text("Cut Off")));
    truncated.resize(truncated.size() - 6);
    write_file(dir / "truncated.mp3", truncated);
    CHECK(parse(dir / "truncated.mp3", meta) == music::ParseResult::Ok);

    Bytes oversized{'I', 'D', '3', 3, 0, 0, 0x7F, 0x7F, 0x7F, 0x7F, 'T', 'I', 'T', '2', 0x7F, 0x7F, 0x7F, 0x7F, 0, 0, 0, 'x'};
    write_file(dir / "oversized.mp3", oversized);
    CHECK(parse(dir / "oversized.mp3", meta) == music::ParseResult::Ok);
    CHECK(meta.title[0] == '\0');

    Bytes garbage(5000);
    for (size_t i = 0; i < garbage.size(); i++) garbage[i] = uint8_t(i * 37 + 11);
    write_file(dir / "garbage.mp3", garbage);
    CHECK(parse(dir / "garbage.mp3", meta) == music::ParseResult::Ok);
    CHECK(!meta.has_tag);

    write_file(dir / "empty.mp3", Bytes{});
    CHECK(parse(dir / "empty.mp3", meta) == music::ParseResult::Ok);
    CHECK(meta.duration_ms == 0);

    CHECK(parse(dir / "missing.mp3", meta) == music::ParseResult::IoError);
    return 0;
}

int test_cancel(const fs::path &dir)
{
    music::TrackMeta meta;
    CallCounter counter;
    counter.cancel_after = 0;
    music::ParseOptions options{counter_cancelled, &counter};
    CHECK(parse(dir / "a.mp3", meta, options) == music::ParseResult::Cancelled);
    CHECK(meta.title[0] == '\0' && meta.duration_ms == 0);

    counter.calls = 0;
    counter.cancel_after = 2;
    CHECK(parse(dir / "cover.mp3", meta, options) == music::ParseResult::Cancelled);
    return 0;
}

int test_library(const fs::path &root)
{
    fs::path music_dir = root / "Music";
    Bytes tag = id3v2(3, text_frame(3, "TIT2", 0, text("Tagged Title")));
    append(tag, mpeg_frames(8));
    write_file(music_dir / "b song.mp3", mpeg_frames(8));
    write_file(music_dir / "A Song.MP3", tag);
    write_file(music_dir / "notes.txt", text("x"));
    write_file(music_dir / "cover.jpg", text("x"));
    write_file(music_dir / ".hidden.mp3", mpeg_frames(8));
    write_file(music_dir / "Rock" / "Anthem.mp3", mpeg_frames(8));
    write_file(music_dir / "Rock" / "readme.md", text("x"));
    write_file(music_dir / "Rock" / "deep" / "Too Deep.mp3", mpeg_frames(8));
    write_file(music_dir / ".trash" / "Gone.mp3", mpeg_frames(8));
    write_file(music_dir / "Rock_Anthem.mp3", mpeg_frames(8));
    write_file(music_dir / "Rock_Anthem #2.mp3", mpeg_frames(8));

    music::Library library(music_dir.string());
    CHECK(library.scan() == 5);
    CHECK(library.count() == 5);

    std::vector<std::string> names;
    for (size_t i = 0; i < library.count(); i++) names.push_back(library.track(i)->display_name);
    const std::vector<std::string> expected{"A Song", "b song", "Rock_Anthem", "Rock_Anthem #2", "Rock_Anthem #2 #2"};
    CHECK(names == expected);
    CHECK(library.track(5) == nullptr);

    CHECK(library.track(0)->state == music::MetaState::Pending);
    CHECK(std::string(music::Library::title_of(*library.track(0))) == "A Song");
    CHECK(library.load_meta(0) == music::ParseResult::Ok);
    CHECK(library.track(0)->state == music::MetaState::Ready);
    CHECK(std::string(music::Library::title_of(*library.track(0))) == "Tagged Title");

    CHECK(library.load_meta(1) == music::ParseResult::Ok);
    CHECK(std::string(music::Library::title_of(*library.track(1))) == "b song");

    CallCounter counter;
    counter.cancel_after = 0;
    music::ParseOptions options{counter_cancelled, &counter};
    CHECK(library.load_meta(2, options) == music::ParseResult::Cancelled);
    CHECK(library.track(2)->state == music::MetaState::Pending);

    fs::remove(library.track(3)->path);
    CHECK(library.load_meta(3) == music::ParseResult::IoError);
    CHECK(library.track(3)->state == music::MetaState::Failed);
    CHECK(library.load_meta(99) == music::ParseResult::IoError);

    music::Library empty((root / "nothing").string());
    CHECK(empty.scan() == 0);
    return 0;
}

int test_samples(const char *sample_dir)
{
    music::Library library(sample_dir);
    const size_t count = library.scan();
    std::printf("samples: %zu tracks\n", count);
    for (size_t i = 0; i < count; i++) {
        CHECK(library.load_meta(i) == music::ParseResult::Ok);
        const music::Track *t = library.track(i);
        std::printf("  %-44s | %s | %s | %s | %u ms\n", t->display_name.c_str(), t->meta.title, t->meta.artist,
                    t->meta.album, t->meta.duration_ms);
        CHECK(t->meta.duration_ms > 1000);
    }
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    char pattern[] = "/tmp/music_library_test_XXXXXX";
    char *base = mkdtemp(pattern);
    if (base == nullptr) return 2;
    const fs::path root(base);
    const fs::path files = root / "files";
    fs::create_directories(files);

    int rc = 0;
    rc = rc ? rc : test_id3v23_latin1(files);
    rc = rc ? rc : test_id3v24_encodings(files);
    rc = rc ? rc : test_unsynchronisation(files);
    rc = rc ? rc : test_tlen_and_xing(files);
    rc = rc ? rc : test_large_cover_is_skipped(files);
    rc = rc ? rc : test_id3v1_and_fallbacks(files);
    rc = rc ? rc : test_corrupt_inputs(files);
    rc = rc ? rc : test_cancel(files);
    rc = rc ? rc : test_library(root / "sd");
    if (rc == 0 && argc > 1) rc = test_samples(argv[1]);

    std::error_code ec;
    fs::remove_all(root, ec);
    if (rc == 0) std::puts("music_library_test passed");
    return rc;
}
