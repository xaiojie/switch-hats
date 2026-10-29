#pragma once
#include "core.hpp"
#include <map>
#include <set>
namespace papa {
struct LyricLine {
  double seconds = 0;
  std::string text;
};
struct Lyrics {
  std::vector<LyricLine> lines;
  std::string plain;
  double offset =
      0; // seconds, LRC [offset:milliseconds]; positive means display earlier.
  int Current(double seconds, double userOffset = 0) const;
};
Lyrics ParseLyrics(const std::string &text);
struct Metadata {
  std::string title, artist, album, albumArtist, year, coverMime, rawLyrics;
  int track = 0, disc = 0;
  std::vector<unsigned char> cover;
  Lyrics lyrics;
};
// Limits: ID3 tag 16 MiB; cover 4 MiB; text/lyrics 1 MiB; catalog/state 8
// MiB/10000 tracks; yyjson allocations capped at 24 MiB. ID3v1 encoding policy:
// valid UTF-8 first, otherwise GBK, invalid bytes become U+FFFD.
enum class LegacyEncoding { UTF8ThenGBK, Latin1 };
Metadata ReadMetadata(const std::string &path,
                      LegacyEncoding policy = LegacyEncoding::UTF8ThenGBK);
std::string ResolveUrl(const std::string &base, const std::string &reference);
std::vector<Track> ParseCatalog(const std::string &text,
                                const std::string &baseUrl,
                                const std::string &sourceId,
                                std::string &error);
// cacheDir must exist. Content fingerprint ID survives path renaming. Only
// paths retained in Track. ID sd:<FNV1a64 hex>, computed from first 64KiB,
// last 64KiB (when file >64KiB), then ':'+decimal byte size. Bounded,
// rename-stable, not a cryptographic identity; tag edits can change it.
// Cloud IDs: cloud:<sourceId>:<catalogId> (URL FNV1a64 if no catalog ID).
void EnrichTrack(Track &track, const std::string &cacheDir);
bool MatchesTrack(const Track &track, const std::string &query);
struct MediaSource {
  std::string id, name, url;
  bool enabled = true;
};
struct MediaPlaylist {
  std::string id, name;
  std::vector<Track> tracks;
};
struct MediaResume {
  Track track;
  double position = 0;
  Mode mode = Mode::LoopAll;
  int volume = 70;
  bool valid = false;
};
struct MediaOverride {
  std::string title, artist, album, cover, lyrics;
};
struct MediaState {
  std::vector<MediaSource> sources;
  std::set<std::string> favorites;
  std::vector<MediaPlaylist> playlists;
  std::map<std::string, double> lyricsOffsets;
  MediaResume resume;
};
// version:1 state JSON: favorites:[id], playlists:[{id,name,tracks:[full
// Track]}], lyricsOffsets:{id:seconds},
// resume:{track:Track,position,mode:0..3,volume,valid}, sources:[...]. Save
// writes .tmp then keeps prior valid file as .bak. Load falls back to .bak on
// malformed primary.
bool SaveMediaState(const std::string &path, const MediaState &state,
                    std::string *error = nullptr);
bool LoadMediaState(const std::string &path, MediaState &state,
                    std::string *error = nullptr);
bool LoadSources(const std::string &path, std::vector<MediaSource> &sources,
                 std::string *error = nullptr);
bool SaveSources(const std::string &path,
                 const std::vector<MediaSource> &sources,
                 std::string *error = nullptr);
bool LoadOverrides(const std::string &path,
                   std::map<std::string, MediaOverride> &overrides,
                   std::string *error = nullptr);
void ApplyOverride(Track &track, const MediaOverride &value);
// Extended M3U8 includes #PAPA-TRACK JSON for source/ID/metadata preservation
// across mixed imports. Import with ParseCatalog(text,"","import",error):
// a marker whose path exactly matches its following URI preserves full Track
// including SD paths and original ID/source. Unmarked entries use base/sourceId
// and normal cloud URL resolution.
std::string ExportPlaylist(const MediaPlaylist &playlist);
} // namespace papa
