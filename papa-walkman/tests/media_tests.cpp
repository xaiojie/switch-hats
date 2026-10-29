#include "media.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
using namespace papa;
#undef assert
#define assert(x)                                                              \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::cerr << __LINE__ << ": " << #x << std::endl;                        \
      return 1;                                                                \
    }                                                                          \
  } while (0)
static void put(const std::string &p, const std::string &s) {
  std::ofstream f(p, std::ios::binary);
  f.write(s.data(), s.size());
}
static std::string frame(std::string id, std::string body, bool v4 = false) {
  unsigned n = body.size();
  std::string h = id;
  for (int i = 3; i >= 0; --i)
    h += char(v4 ? (n >> (7 * i)) & 127 : (n >> (8 * i)) & 255);
  return h + std::string(2, 0) + body;
}
static std::string tag(std::string body, int v = 3) {
  std::string h = "ID3";
  h += char(v);
  h += std::string(2, 0);
  for (int i = 3; i >= 0; --i)
    h += char((body.size() >> (7 * i)) & 127);
  return h + body;
}

static bool reviewRegressions() {
  int failed = 0;
  auto expect = [&](bool result, const char *name) {
    if (!result) {
      ++failed;
      std::cerr << "REVIEW FAIL: " << name << "\n";
    }
  };
  std::string repeated;
  for (int i = 0; i < 20; ++i)
    repeated += "[00:01]";
  repeated += std::string(64 * 1024, 'x');
  auto bounded = ParseLyrics(repeated);
  size_t expanded = bounded.plain.size();
  for (const auto &line : bounded.lines)
    expanded += line.text.size();
  expect(expanded <= 1024 * 1024, "aggregate expanded LRC text <=1MiB");
  std::string error;
  std::string marker = "#PAPA-TRACK "
                       "{\"id\":\"sd:stale\",\"path\":\"https://a/"
                       "later.mp3\",\"source\":\"SD\"}\n";
  auto playlist = ParseCatalog("#EXTM3U\n" + marker +
                                   "https://a/first.mp3\nhttps://a/later.mp3\n",
                               "https://a/list", "s", error);
  expect(playlist.size() == 2 && playlist[1].source == SourceKind::Cloud &&
             playlist[1].id != "sd:stale",
         "mismatching URI consumes marker");
  playlist = ParseCatalog("#EXTM3U\n" + marker +
                              "#PAPA-TRACK {broken\nhttps://a/later.mp3\n",
                          "https://a/list", "s", error);
  expect(playlist.size() == 1 && playlist[0].source == SourceKind::Cloud,
         "invalid replacement clears marker");
  for (const char *value : {"1e999", "NaN", "Infinity", "7junk", "garbage"}) {
    playlist = ParseCatalog(std::string("#EXTM3U\n#EXTINF:") + value +
                                ",title\na.mp3\n",
                            "https://a/list", "s", error);
    expect(playlist.size() == 1 && playlist[0].duration == 0,
           "invalid EXTINF duration becomes unknown");
  }
  const std::string path = "build-host/media-review-state.json";
  std::remove(path.c_str());
  std::remove((path + ".bak").c_str());
  MediaState prior;
  prior.favorites.insert("keep");
  MediaState result = prior;
  std::string entries;
  for (int i = 0; i < 5001; ++i) {
    if (i)
      entries += ',';
    entries += R"({"id":"sd:x","path":"a"})";
  }
  std::string large =
      R"({"version":1,"favorites":[],"playlists":[{"id":"a","name":"a","tracks":[)" +
      entries + R"(]},{"id":"b","name":"b","tracks":[)" +
      entries.substr(0, entries.rfind(",{")) +
      R"(]}],"resume":{"valid":false}})";
  put(path, large);
  expect(!LoadMediaState(path, result, &error) &&
             result.favorites == prior.favorites,
         "aggregate 10001 tracks rejected without changing caller");
  put(path,
      R"({"version":1,"favorites":[],"playlists":[{"id":"a","tracks":[{}]}]})");
  expect(!LoadMediaState(path, result, &error) &&
             result.favorites == prior.favorites,
         "malformed track rejected before materialization");
  MediaState over;
  Track track;
  track.id = "sd:x";
  track.path = "a";
  over.playlists.push_back({"a", "a", std::vector<Track>(5001, track)});
  over.playlists.push_back({"b", "b", std::vector<Track>(5000, track)});
  expect(!SaveMediaState(path, over, &error),
         "save aggregate 10001 tracks rejected");
  over.playlists[1].tracks.pop_back();
  over.resume.valid = true;
  over.resume.track = track;
  expect(!SaveMediaState(path, over, &error),
         "valid resume included in aggregate track quota");

  std::string fiveThousand = entries.substr(0, entries.rfind(",{"));
  std::string boundary =
      R"({"version":1,"favorites":[],"playlists":[{"tracks":[)" + fiveThousand +
      R"(]},{"tracks":[)" + fiveThousand + R"(]}],"resume":{"valid":false}})";
  put(path, boundary);
  expect(LoadMediaState(path, result, &error) && result.playlists.size() == 2 &&
             result.playlists[0].tracks.size() == 5000 &&
             result.playlists[1].tracks.size() == 5000,
         "exact 10000-track load boundary accepted");
  auto validPosition = boundary.find("\"valid\":false");
  boundary.replace(validPosition, 13,
                   R"("valid":true,"track":{"id":"sd:x","path":"a"})");
  result = prior;
  put(path, boundary);
  expect(!LoadMediaState(path, result, &error) &&
             result.favorites == prior.favorites,
         "load resume included in aggregate track quota");
  over.resume.valid = false;
  expect(SaveMediaState(path, over, &error),
         "exact 10000-track save boundary accepted");
  MediaState malformed;
  malformed.playlists.push_back({"bad", "bad", {Track{}}});
  expect(!SaveMediaState(path, malformed, &error),
         "save malformed Track rejected");
  expect(SaveMediaState(path, prior, &error) &&
             SaveMediaState(path, prior, &error),
         "create recoverable state fixture");
  over.resume.valid = true;
  expect(!SaveMediaState(path, over, &error),
         "overquota save leaves valid primary intact");
  result = MediaState{};
  expect(LoadMediaState(path, result, &error) &&
             result.favorites == prior.favorites,
         "failed save preserved primary state");
  put(path, large);
  result = MediaState{};
  expect(LoadMediaState(path, result, &error) &&
             result.favorites == prior.favorites,
         "overquota primary recovers valid backup");
  std::cout << "review regressions: " << failed << " failures\n";
  return failed == 0;
}
int main(int argc, char **argv) {
  if (argc == 3 && std::string(argv[1]) == "--smoke") {
    std::ifstream list(argv[2]);
    std::string path;
    size_t files = 0, titles = 0, covers = 0;
    while (std::getline(list, path)) {
      if (!path.empty() && path.back() == '\r')
        path.pop_back();
      auto metadata = ReadMetadata(path);
      ++files;
      if (!metadata.title.empty())
        ++titles;
      if (!metadata.cover.empty())
        ++covers;
    }
    assert(files > 0 && titles > 0);
    std::cout << "read-only music smoke: " << files << " files, " << titles
              << " titles, " << covers << " embedded covers\n";
    return 0;
  }

  if (!reviewRegressions())
    return 1;
  auto l = ParseLyrics("\xef\xbb\xbf[ar:Test]\n[offset:250]\n[00:02.50][00:01."
                       "00]歌词\n[00:03]end");
  assert(l.lines.size() == 3 && l.lines[0].text == "歌词");
  assert(l.Current(.7) == -1);
  assert(l.Current(.8) == 0);
  assert(l.Current(0, 1) == 0);
  assert(ParseLyrics("plain\ntext").lines.empty());
  assert(ParseLyrics("plain\ntext").plain == "plain\ntext");
  put("build-host/media-v1.mp3",
      std::string(100, 'x') + "TAG" + std::string("\xb5\xbe\xcf\xe3") +
          std::string(26, 0) + std::string("\xd6\xdc\xbd\xdc\xc2\xd7") +
          std::string(24, 0) + std::string(65, 0));
  auto m = ReadMetadata("build-host/media-v1.mp3");
  assert(m.title == "稻香" && m.artist == "周杰伦");
  auto body =
      frame("TIT2", std::string("\x01\xff\xfe") + std::string("\x3c\x7a", 2)) +
      frame("TALB", std::string("\x03") + "Album") +
      frame("APIC", std::string("\x03image/png\0\x03\0", 13) + "PNG") +
      frame("USLT", std::string("\x03"
                                "eng\0",
                                5) +
                        "plain lyrics");
  put("build-host/media-v2.mp3", tag(body));
  m = ReadMetadata("build-host/media-v2.mp3");
  assert(m.title == "稼" && m.album == "Album");
  assert(m.cover.size() == 3 && m.rawLyrics == "plain lyrics");
  put("build-host/media-v4.mp3",
      tag(frame("TIT2", std::string("\x03") + "Chinese 中文", true), 4));
  assert(ReadMetadata("build-host/media-v4.mp3").title == "Chinese 中文");
  put("build-host/media-bad.mp3", tag("TIT2\x7f\x7f\x7f\x7f\0\0bad"));
  assert(ReadMetadata("build-host/media-bad.mp3").title.empty());
  std::string err;
  auto tracks = ParseCatalog(
      R"({"version":1,"tracks":[{"id":"a","title":"曲","audio":"../曲 目.mp3","cover":"cover.png"}]})",
      "https://a/b/list.json", "src", err);
  assert(err.empty() && tracks.size() == 1 && tracks[0].id == "cloud:src:a");
  assert(tracks[0].path == "https://a/%E6%9B%B2%20%E7%9B%AE.mp3");
  assert(tracks[0].duration == 0);
  assert(ResolveUrl("https://a/b/list?q=2", "?x=1") == "https://a/b/list?x=1");
  assert(ResolveUrl("https://a/b/list", "//b/a%20b") == "https://b/a%20b");
  auto mu = ParseCatalog("#EXTM3U\n#EXTINF:-1,Artist - Title\n../a.mp3\n",
                         "https://a/b/list", "s", err);
  assert(mu.size() == 1 && mu[0].artist == "Artist" && mu[0].duration == 0);
  assert(ParseCatalog("#EXTM3U\n#EXT-X-TARGETDURATION:2\na.mp3", "https://a/l",
                      "s", err)
             .empty() &&
         !err.empty());
  Track t{"file", "稻香", "周杰伦"};
  t.album = "跨时代";
  t.id = "sd:example";
  assert(MatchesTrack(t, "dx") && MatchesTrack(t, "zjl") &&
         MatchesTrack(t, "稻") && !MatchesTrack(t, "foobar"));
  MediaState s;
  s.sources.push_back({"s", "Cloud", "https://a/l", true});
  s.favorites.insert("cloud:s:a");
  s.playlists.push_back({"mix", "Mixed", {t, tracks[0]}});
  s.lyricsOffsets[t.id] = .25;
  s.resume = {tracks[0], 12.5, Mode::Shuffle, 40, true};
  assert(SaveMediaState("build-host/media-state.json", s, &err));
  MediaState r;
  assert(LoadMediaState("build-host/media-state.json", r, &err));
  assert(r.resume.valid && r.resume.position == 12.5 &&
         r.playlists[0].tracks[1].id == tracks[0].id);
  assert(SaveMediaState("build-host/media-state.json", s, &err));
  put("build-host/media-state.json", "{");
  assert(LoadMediaState("build-host/media-state.json", r, &err));
  auto playlist = ExportPlaylist(s.playlists[0]);
  auto back = ParseCatalog(playlist, "", "import", err);
  assert(back.size() == 2 && back[0].path == t.path &&
         back[1].id == tracks[0].id && back[0].id == t.id &&
         back[0].source == SourceKind::SD &&
         back[1].source == SourceKind::Cloud);
  Track a{"build-host/media-v1.mp3", "fallback", "unknown"};
  EnrichTrack(a, "build-host");
  assert(a.title == "稻香" && !a.id.empty());
  std::rename(a.path.c_str(), "build-host/media-renamed.mp3");
  Track b{"build-host/media-renamed.mp3", "x", "x"};
  EnrichTrack(b, "build-host");
  assert(a.id == b.id);

  // Frame-specific SYLT milliseconds; frame-count timestamps must not
  // masquerade as seconds.
  std::string sylt = std::string("\x03"
                                 "eng",
                                 4) +
                     std::string("\x02\x01\0", 3) + "line" + std::string(1, 0) +
                     std::string("\0\0\x03\xe8", 4);
  put("build-host/media-sylt.mp3", tag(frame("SYLT", sylt)));
  m = ReadMetadata("build-host/media-sylt.mp3");
  assert(m.lyrics.lines.size() == 1 && m.lyrics.lines[0].seconds == 1 &&
         m.lyrics.lines[0].text == "line");
  sylt[4] = 1;
  put("build-host/media-sylt.mp3", tag(frame("SYLT", sylt)));
  assert(ReadMetadata("build-host/media-sylt.mp3").lyrics.lines.empty());
  put("build-host/media-latin.mp3",
      tag(frame("TIT2", std::string("\0\xe9", 2))));
  assert(ReadMetadata("build-host/media-latin.mp3").title == "é");
  put("build-host/media-utf16be.mp3",
      tag(frame("TIT2", std::string("\x02\x7a\x3c", 3))));
  assert(ReadMetadata("build-host/media-utf16be.mp3").title == "稼");
  auto badSize = tag("");
  badSize[6] = char(0x80);
  put("build-host/media-size.mp3", badSize);
  assert(ReadMetadata("build-host/media-size.mp3").title.empty());
  assert(ParseLyrics(std::string("[00:01]\xb5\xbe\xcf\xe3")).lines[0].text ==
         "稻香");
  assert(ParseLyrics("[00:05]five\n[00:02]two\n[00:02]also two").Current(2) ==
         1);
  assert(ParseLyrics("[00:99]bad").lines.empty());
  assert(ParseCatalog(R"({"version":2,"tracks":[]})", "https://a/l", "s", err)
             .empty() &&
         !err.empty());
  assert(ParseCatalog(R"({"version":1,"tracks":[{"audio":"ftp://a/x"}]})",
                      "https://a/l", "s", err)
             .empty() &&
         !err.empty());
  assert(
      ParseCatalog(
          R"({"version":1,"tracks":[{"id":"x","audio":"a"},{"id":"x","audio":"b"}]})",
          "https://a/l", "s", err)
          .empty() &&
      !err.empty());
  std::vector<MediaSource> sources;
  assert(SaveSources("build-host/media-sources.json", s.sources, &err));
  assert(LoadSources("build-host/media-sources.json", sources, &err) &&
         sources[0].id == "s");
  put("build-host/media-overrides.json",
      R"({"version":1,"tracks":{"x":{"title":"Override","cover":"https://a/c.png"}}})");
  std::map<std::string, MediaOverride> overrides;
  assert(LoadOverrides("build-host/media-overrides.json", overrides, &err));
  Track overridden;
  ApplyOverride(overridden, overrides["x"]);
  assert(overridden.title == "Override" &&
         overridden.cover == "https://a/c.png");
  put("build-host/media-state.json", "{\"version\":99}");
  assert(LoadMediaState("build-host/media-state.json", r, &err));
  std::remove("build-host/media-state.json");
  assert(LoadMediaState("build-host/media-state.json", r, &err));
  Track artwork;
  artwork.path = "build-host/media-v2.mp3";
  EnrichTrack(artwork, "build-host");
  assert(!artwork.cover.empty() && !artwork.lyrics.empty());

  std::string uns = frame("TIT2", std::string("\0\xff\0\xe1", 4), true);
  uns[9] = 2;
  put("build-host/media-unsync.mp3", tag(uns, 4));
  assert(ReadMetadata("build-host/media-unsync.mp3").title == "ÿá");
  std::string dli =
      frame("TIT2", std::string("\0\0\0\x06\x03", 5) + "title", true);
  dli[9] = 1;
  put("build-host/media-dli.mp3", tag(dli, 4));
  assert(ReadMetadata("build-host/media-dli.mp3").title == "title");
  std::string extended(6, 0);
  extended[3] = 6;
  extended[4] = 1;
  auto extTag =
      tag(extended + frame("TIT2", std::string("\x03") + "extended", true), 4);
  extTag[5] = 0x40;
  put("build-host/media-extended.mp3", extTag);
  assert(ReadMetadata("build-host/media-extended.mp3").title == "extended");
  std::string full = R"({"version":1,"tracks":[)";
  for (int j = 0; j < 10001; ++j) {
    if (j)
      full += ',';
    full += R"({"audio":"a"})";
  }
  full += "]}";
  assert(ParseCatalog(full, "https://a/l", "s", err).empty() && !err.empty());
  assert(ResolveUrl("https://a/b/c", "../d%20e.mp3") == "https://a/d%20e.mp3");

  assert(ResolveUrl("https://a/b", "100%.mp3") == "https://a/100%25.mp3");
  assert(ResolveUrl("https://a/b", "ok%20name.mp3") ==
         "https://a/ok%20name.mp3");
  put("build-host/media-state.json", R"({"version":1,"favorites":"broken"})");
  assert(LoadMediaState("build-host/media-state.json", r, &err) &&
         r.playlists.size() == 1);
  put("build-host/media-surrogate.mp3",
      tag(frame("TIT2", std::string("\x01\xff\xfe\x3d\xd8\x00\xde", 7))));
  assert(ReadMetadata("build-host/media-surrogate.mp3").title == "😀");
  std::string density = R"({"version":1,"tracks":[)";
  for (int i = 0; i < 1500000; ++i)
    density += "0,";
  density += "0]}";
  assert(ParseCatalog(density, "https://a/l", "s", err).empty() &&
         !err.empty());
  put("build-host/media-offset.mp3",
      tag(frame("USLT", std::string("\x03"
                                    "eng\0",
                                    5) +
                            "[offset:500]\n[00:01]first\n[00:59.999]edge")));
  Track offsetTrack;
  offsetTrack.path = "build-host/media-offset.mp3";
  EnrichTrack(offsetTrack, "build-host");
  std::ifstream offsetFile(offsetTrack.lyrics);
  std::string cachedLyrics((std::istreambuf_iterator<char>(offsetFile)),
                           std::istreambuf_iterator<char>());
  assert(ParseLyrics(cachedLyrics).offset == .5 &&
         ParseLyrics(cachedLyrics).lines.size() == 2);
  std::cout << "media tests passed\n";
}
