#include "media.hpp"
#include "media_tables.hpp"
#include "yyjson.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <wchar.h>
#include <windows.h>
#endif
namespace papa {
namespace {
constexpr size_t TagLimit = 16 * 1024 * 1024, TextLimit = 1024 * 1024,
                 CoverLimit = 4 * 1024 * 1024, JsonLimit = 8 * 1024 * 1024,
                 CountLimit = 10000;
FILE *openFile(const std::string &p, const char *mode) {
#ifdef _WIN32
  auto wide = [](const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                int(s.size()), nullptr, 0);
    std::wstring w(n, 0);
    if (n)
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                          int(s.size()), &w[0], n);
    return w;
  };
  return _wfopen(wide(p).c_str(), wide(mode).c_str());
#else
  return fopen(p.c_str(), mode);
#endif
}
int removeFile(const std::string &p) {
#ifdef _WIN32
  int n = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
  std::wstring w(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, &w[0], n);
  return (DeleteFileW(w.c_str()) ? 0 : -1);
#else
  return remove(p.c_str());
#endif
}
int renameFile(const std::string &a, const std::string &b) {
#ifdef _WIN32
  auto w = [](const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &r[0], n);
    return r;
  };
  return (MoveFileW(w(a).c_str(), w(b).c_str()) ? 0 : -1);
#else
  return rename(a.c_str(), b.c_str());
#endif
}
bool readFile(const std::string &p, std::string &out, size_t cap = JsonLimit) {
  FILE *f = openFile(p, "rb");
  if (!f)
    return false;
  out.clear();
  char b[8192];
  while (size_t n = fread(b, 1, sizeof b, f)) {
    if (out.size() + n > cap) {
      fclose(f);
      out.clear();
      return false;
    }
    out.append(b, n);
  }
  bool ok = !ferror(f);
  fclose(f);
  return ok;
}
bool writeFile(const std::string &p, const std::string &s) {
  FILE *f = openFile(p, "wb");
  if (!f)
    return false;
  bool ok = fwrite(s.data(), 1, s.size(), f) == s.size();
  ok = (fflush(f) == 0) && ok;
  return fclose(f) == 0 && ok;
}
void utf8(std::string &o, uint32_t c) {
  if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
    c = 0xfffd;
  if (c < 128)
    o += char(c);
  else if (c < 2048) {
    o += char(192 | (c >> 6));
    o += char(128 | (c & 63));
  } else if (c < 65536) {
    o += char(224 | (c >> 12));
    o += char(128 | ((c >> 6) & 63));
    o += char(128 | (c & 63));
  } else {
    o += char(240 | (c >> 18));
    o += char(128 | ((c >> 12) & 63));
    o += char(128 | ((c >> 6) & 63));
    o += char(128 | (c & 63));
  }
}
uint32_t nextUtf(const std::string &s, size_t &i, bool &ok) {
  unsigned c = (unsigned char)s[i++];
  if (c < 128)
    return c;
  int n = c >= 0xc2 && c <= 0xdf   ? 1
          : c >= 0xe0 && c <= 0xef ? 2
          : c >= 0xf0 && c <= 0xf4 ? 3
                                   : -1;
  if (n < 0 || i + n > s.size()) {
    ok = false;
    return 0xfffd;
  }
  uint32_t v = c & ((1 << (6 - n)) - 1);
  for (int k = 0; k < n; ++k) {
    unsigned b = (unsigned char)s[i];
    if ((b & 192) != 128) {
      ok = false;
      return 0xfffd;
    }
    ++i;
    v = (v << 6) | (b & 63);
  }
  if ((n == 1 && v < 128) || (n == 2 && v < 2048) || (n == 3 && v < 65536) ||
      v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) {
    ok = false;
    return 0xfffd;
  }
  return v;
}
std::string cleanUtf(const std::string &s, bool *valid = nullptr) {
  std::string o;
  bool ok = true;
  for (size_t i = 0; i < s.size();)
    utf8(o, nextUtf(s, i, ok));
  if (valid)
    *valid = ok;
  return o;
}
std::string legacy(const std::string &s) {
  bool valid;
  auto u = cleanUtf(s, &valid);
  if (valid)
    return u;
  std::string o;
  for (size_t i = 0; i < s.size();) {
    unsigned a = (unsigned char)s[i++];
    if (a < 128)
      utf8(o, a);
    else if (a >= 0x81 && a <= 0xfe && i < s.size() &&
             (unsigned char)s[i] >= 0x40 && (unsigned char)s[i] <= 0xfe) {
      unsigned b = (unsigned char)s[i++];
      utf8(o, tables::gbk[(a - 0x81) * 191 + b - 0x40]);
    } else
      utf8(o, 0xfffd);
  }
  return o;
}
std::string trim(std::string s) {
  while (!s.empty() && (s.back() == 0 || s.back() == ' ' || s.back() == '\r' ||
                        s.back() == '\n' || s.back() == '\t'))
    s.pop_back();
  size_t n = 0;
  while (n < s.size() &&
         (s[n] == ' ' || s[n] == '\r' || s[n] == '\n' || s[n] == '\t'))
    ++n;
  return s.substr(n);
}
std::string decode(const std::string &s, unsigned enc) {
  if (enc == 0) {
    std::string out;
    for (unsigned char c : s)
      utf8(out, c);
    return out;
  }
  if (enc == 3)
    return cleanUtf(s);
  if (enc != 1 && enc != 2)
    return {};
  size_t i = 0;
  bool le = false;
  if (s.size() >= 2) {
    if ((unsigned char)s[0] == 0xff && (unsigned char)s[1] == 0xfe) {
      le = true;
      i = 2;
    } else if ((unsigned char)s[0] == 0xfe && (unsigned char)s[1] == 0xff)
      i = 2;
  }
  std::string o;
  auto word = [&](size_t at) {
    return le ? uint32_t((unsigned char)s[at] | ((unsigned char)s[at + 1] << 8))
              : uint32_t(((unsigned char)s[at] << 8) |
                         (unsigned char)s[at + 1]);
  };
  while (i + 1 < s.size()) {
    uint32_t c = word(i);
    i += 2;
    if (c >= 0xd800 && c <= 0xdbff && i + 1 < s.size()) {
      uint32_t d = word(i);
      if (d >= 0xdc00 && d <= 0xdfff) {
        c = 0x10000 + ((c - 0xd800) << 10) + d - 0xdc00;
        i += 2;
      }
    }
    utf8(o, c);
  }
  if (i < s.size())
    utf8(o, 0xfffd);
  return o;
}
size_t terminated(const std::string &s, size_t at, unsigned enc) {
  size_t step = enc == 1 || enc == 2 ? 2 : 1;
  for (size_t i = at; i + step <= s.size(); i += step)
    if (s[i] == 0 && (step == 1 || s[i + 1] == 0))
      return i + step;
  return s.size();
}
uint32_t be(const unsigned char *p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 |
         p[3];
}
bool syncSize(const unsigned char *p, uint32_t &v) {
  if ((p[0] | p[1] | p[2] | p[3]) & 128)
    return false;
  v = uint32_t(p[0]) << 21 | uint32_t(p[1]) << 14 | uint32_t(p[2]) << 7 | p[3];
  return true;
}
std::string unsync(const std::string &s) {
  std::string o;
  for (size_t i = 0; i < s.size(); ++i) {
    o += s[i];
    if ((unsigned char)s[i] == 255 && i + 1 < s.size() && s[i + 1] == 0)
      ++i;
  }
  return o;
}
std::string hash(const std::string &s) {
  uint64_t h = 14695981039346656037ull;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ull;
  }
  std::ostringstream o;
  o << std::hex << std::setfill('0') << std::setw(16) << h;
  return o.str();
}
bool http(const std::string &s) {
  return s.compare(0, 7, "http://") == 0 || s.compare(0, 8, "https://") == 0;
}
std::string encodeUrl(const std::string &s) {
  static const char hex[] = "0123456789ABCDEF";
  std::string o;
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    bool invalidPercent =
        c == '%' && (i + 2 >= s.size() ||
                     !std::isxdigit(static_cast<unsigned char>(s[i + 1])) ||
                     !std::isxdigit(static_cast<unsigned char>(s[i + 2])));
    if (invalidPercent || c <= 32 || c >= 127 || c == '"' || c == '<' ||
        c == '>' || c == '\\') {
      o += '%';
      o += hex[c >> 4];
      o += hex[c & 15];
    } else
      o += char(c);
  }
  return o;
}
// Parsing allocations have a hard quota independent of hostile JSON node
// density.
struct JsonQuota {
  size_t used = 0;
};
struct alignas(std::max_align_t) JsonBlock {
  size_t size;
};
constexpr size_t JsonMemoryLimit = 24 * 1024 * 1024;
void *jsonAlloc(void *ctx, size_t n) {
  auto &q = *static_cast<JsonQuota *>(ctx);
  if (n > JsonMemoryLimit - q.used)
    return nullptr;
  auto *b = static_cast<JsonBlock *>(std::malloc(sizeof(JsonBlock) + n));
  if (!b)
    return nullptr;
  b->size = n;
  q.used += n;
  return b + 1;
}
void jsonFree(void *ctx, void *ptr) {
  if (!ptr)
    return;
  auto *b = static_cast<JsonBlock *>(ptr) - 1;
  static_cast<JsonQuota *>(ctx)->used -= b->size;
  std::free(b);
}
void *jsonRealloc(void *ctx, void *ptr, size_t, size_t n) {
  if (!ptr)
    return jsonAlloc(ctx, n);
  auto *old = static_cast<JsonBlock *>(ptr) - 1;
  auto &q = *static_cast<JsonQuota *>(ctx);
  if (n > old->size && n - old->size > JsonMemoryLimit - q.used)
    return nullptr;
  size_t oldSize = old->size;
  auto *b = static_cast<JsonBlock *>(std::realloc(old, sizeof(JsonBlock) + n));
  if (!b)
    return nullptr;
  b->size = n;
  q.used = q.used - oldSize + n;
  return b + 1;
}
struct JsonDelete {
  std::shared_ptr<JsonQuota> quota;
  void operator()(yyjson_doc *d) const { yyjson_doc_free(d); }
};
using Doc = std::unique_ptr<yyjson_doc, JsonDelete>;
Doc json(const std::string &s) {
  auto quota = std::make_shared<JsonQuota>();
  yyjson_alc alc{jsonAlloc, jsonRealloc, jsonFree, quota.get()};
  return Doc(s.size() <= JsonLimit
                 ? yyjson_read_opts(const_cast<char *>(s.data()), s.size(), 0,
                                    &alc, nullptr)
                 : nullptr,
             JsonDelete{quota});
}
std::string str(yyjson_val *v, const char *k) {
  auto x = yyjson_obj_get(v, k);
  auto p = yyjson_get_str(x);
  return p ? std::string(p, yyjson_get_len(x)) : std::string();
}
double num(yyjson_val *v, const char *k) {
  auto x = yyjson_obj_get(v, k);
  return yyjson_is_num(x) ? yyjson_get_num(x) : 0;
}
int integer(yyjson_val *v, const char *k, int maximum = 1000000) {
  double value = num(v, k);
  return std::isfinite(value)
             ? int(std::max(0., std::min(double(maximum), value)))
             : 0;
}
std::string quote(const std::string &s) {
  std::string o = "\"";
  for (unsigned char c : cleanUtf(s)) {
    if (c == '"' || c == '\\') {
      o += '\\';
      o += char(c);
    } else if (c < 32) {
      char b[7];
      snprintf(b, sizeof b, "\\u%04x", c);
      o += b;
    } else
      o += char(c);
  }
  return o + '"';
}
std::string number(double v) {
  if (!std::isfinite(v))
    return "0";
  std::ostringstream o;
  o << std::setprecision(15) << v;
  return o.str();
}
std::string trackJson(const Track &t) {
  return "{\"path\":" + quote(t.path) + ",\"title\":" + quote(t.title) +
         ",\"artist\":" + quote(t.artist) + ",\"id\":" + quote(t.id) +
         ",\"album\":" + quote(t.album) +
         ",\"albumArtist\":" + quote(t.albumArtist) +
         ",\"cover\":" + quote(t.cover) + ",\"lyrics\":" + quote(t.lyrics) +
         ",\"sourceId\":" + quote(t.sourceId) +
         ",\"trackNumber\":" + number(t.trackNumber) +
         ",\"discNumber\":" + number(t.discNumber) +
         ",\"duration\":" + number(t.duration) + ",\"source\":" +
         quote(t.source == SourceKind::Cloud ? "Cloud" : "SD") + "}";
}
Track trackRead(yyjson_val *v) {
  Track t;
  t.path = str(v, "path");
  t.title = str(v, "title");
  t.artist = str(v, "artist");
  t.id = str(v, "id");
  t.album = str(v, "album");
  t.albumArtist = str(v, "albumArtist");
  t.cover = str(v, "cover");
  t.lyrics = str(v, "lyrics");
  t.sourceId = str(v, "sourceId");
  t.trackNumber = integer(v, "trackNumber");
  t.discNumber = integer(v, "discNumber");
  t.duration = std::max(0., num(v, "duration"));
  t.source = str(v, "source") == "Cloud" ? SourceKind::Cloud : SourceKind::SD;
  return t;
}
bool validVersion(yyjson_val *r) {
  return yyjson_is_obj(r) && num(r, "version") == 1;
}
bool documentShape(yyjson_val *r, const char *key, bool object) {
  if (!validVersion(r))
    return false;
  auto value = yyjson_obj_get(r, key);
  return object ? yyjson_is_obj(value) : yyjson_is_arr(value);
}
bool recoverRead(const std::string &p, std::string &s, const char *key,
                 bool object = false) {
  if (readFile(p, s)) {
    auto d = json(s);
    if (d && documentShape(yyjson_doc_get_root(d.get()), key, object))
      return true;
  }
  if (readFile(p + ".bak", s)) {
    auto d = json(s);
    if (d && documentShape(yyjson_doc_get_root(d.get()), key, object))
      return true;
  }
  return false;
}
bool atomicSave(const std::string &p, const std::string &s, std::string *err,
                const char *key, bool object = false) {
  if (s.size() > JsonLimit || !writeFile(p + ".tmp", s)) {
    if (err)
      *err = "write failed";
    return false;
  }
  std::string old;
  bool good = readFile(p, old);
  if (good) {
    auto d = json(old);
    good = d && documentShape(yyjson_doc_get_root(d.get()), key, object);
  }
  if (good) {
    removeFile(p + ".bak");
    if (renameFile(p, p + ".bak") != 0) {
      if (err)
        *err = "backup failed";
      return false;
    }
  } else
    removeFile(p);
  if (renameFile(p + ".tmp", p) != 0) {
    if (good)
      renameFile(p + ".bak", p);
    if (err)
      *err = "replace failed";
    return false;
  }
  if (err)
    err->clear();
  return true;
}
std::string sourceJson(const std::vector<MediaSource> &s) {
  std::string o = "[";
  for (size_t i = 0; i < s.size() && i < CountLimit; ++i) {
    if (i)
      o += ',';
    o += "{\"id\":" + quote(s[i].id) + ",\"name\":" + quote(s[i].name) +
         ",\"url\":" + quote(s[i].url) +
         ",\"enabled\":" + (s[i].enabled ? "true" : "false") + "}";
  }
  return o + "]";
}
void sourceRead(yyjson_val *r, std::vector<MediaSource> &out) {
  auto a = yyjson_obj_get(r, "sources");
  size_t i, n;
  yyjson_val *v;
  yyjson_arr_foreach(a, i, n, v) {
    if (out.size() >= CountLimit)
      break;
    MediaSource s{str(v, "id"), str(v, "name"), str(v, "url"), true};
    auto e = yyjson_obj_get(v, "enabled");
    if (yyjson_is_bool(e))
      s.enabled = yyjson_get_bool(e);
    if (!s.id.empty() && http(s.url))
      out.push_back(s);
  }
}
} // namespace
int Lyrics::Current(double seconds, double userOffset) const {
  double now = seconds + offset + userOffset;
  if (!std::isfinite(now) || lines.empty())
    return -1;
  auto i = std::upper_bound(
      lines.begin(), lines.end(), now,
      [](double t, const LyricLine &l) { return t < l.seconds; });
  return i == lines.begin() ? -1 : int(i - lines.begin() - 1);
}
Lyrics ParseLyrics(const std::string &raw) {
  Lyrics l;
  if (raw.size() > TextLimit)
    return l;
  std::string text = legacy(raw);
  if (text.compare(0, 3, "\xef\xbb\xbf") == 0)
    text.erase(0, 3);
  std::istringstream in(text);
  std::string s;
  while (std::getline(in, s)) {
    if (!s.empty() && s.back() == '\r')
      s.pop_back();
    size_t pos = 0;
    std::vector<double> times;
    while (pos < s.size() && s[pos] == '[') {
      auto end = s.find(']', pos);
      if (end == std::string::npos)
        break;
      std::string token = s.substr(pos + 1, end - pos - 1);
      if (token.compare(0, 7, "offset:") == 0) {
        char *e;
        double v = strtod(token.c_str() + 7, &e);
        if (*e == 0 && std::isfinite(v))
          l.offset = v / 1000.;
      } else {
        char *e;
        double m = strtod(token.c_str(), &e);
        if (e != token.c_str() && *e == ':' && m >= 0) {
          char *last;
          double sec = strtod(e + 1, &last);
          if (last != e + 1 && *last == 0 && sec >= 0 && sec < 60 &&
              std::isfinite(m) && std::isfinite(sec))
            times.push_back(m * 60 + sec);
        }
      }
      pos = end + 1;
    }
    if (!times.empty()) {
      for (double t : times)
        if (l.lines.size() < CountLimit)
          l.lines.push_back({t, s.substr(pos)});
    } else if (pos == 0) {
      if (!l.plain.empty())
        l.plain += '\n';
      l.plain += s;
    }
  }
  std::stable_sort(l.lines.begin(), l.lines.end(),
                   [](const LyricLine &a, const LyricLine &b) {
                     return a.seconds < b.seconds;
                   });
  if (!l.lines.empty())
    l.plain.clear();
  return l;
}
Metadata ReadMetadata(const std::string &path, LegacyEncoding policy) {
  Metadata m;
  FILE *f = openFile(path, "rb");
  if (!f)
    return m;
  unsigned char h[10];
  size_t got = fread(h, 1, 10, f);
  if (got == 10 && memcmp(h, "ID3", 3) == 0 && (h[3] == 3 || h[3] == 4)) {
    uint32_t n;
    unsigned version = h[3], flags = h[5];
    if (syncSize(h + 6, n) && n <= TagLimit && !(flags & 0x0f)) {
      std::string data(n, 0);
      if (fread(&data[0], 1, n, f) == n) {
        if (version == 3 && (flags & 128))
          data = unsync(data);
        size_t at = 0;
        if (flags & 64) {
          if (data.size() < 4)
            at = data.size();
          else {
            uint32_t ext = 0;
            if (version == 4) {
              if (!syncSize((const unsigned char *)data.data(), ext) || ext < 6)
                at = data.size();
              else
                at = ext;
            } else
              at = size_t(be((const unsigned char *)data.data())) + 4;
          }
        }
        while (at + 10 <= data.size()) {
          const unsigned char *p = (const unsigned char *)data.data() + at;
          std::string id((const char *)p, 4);
          if (!std::all_of(id.begin(), id.end(), [](char c) {
                return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
              }))
            break;
          uint32_t len = 0;
          if (version == 4) {
            if (!syncSize(p + 4, len))
              break;
          } else
            len = be(p + 4);
          if (len > data.size() - at - 10)
            break;
          unsigned ff = p[9];
          std::string b = data.substr(at + 10, len);
          at += 10 + len;
          if ((version == 3 && (ff & 0xc0)) || (version == 4 && (ff & 0xbc)))
            continue; // Encrypted/compressed/reserved frame formats are
                      // skipped.
          if (version == 4 && ((flags & 128) || (ff & 2)))
            b = unsync(b);
          if ((version == 3 && (ff & 0x20)) || (version == 4 && (ff & 0x40))) {
            if (b.empty())
              continue;
            b.erase(0, 1);
          }
          if (version == 4 && (ff & 1)) {
            uint32_t decoded = 0;
            if (b.size() < 4 ||
                !syncSize(reinterpret_cast<const unsigned char *>(b.data()),
                          decoded) ||
                decoded != b.size() - 4)
              continue;
            b.erase(0, 4);
          }
          if (b.empty())
            continue;
          unsigned enc = (unsigned char)b[0];
          if (id[0] == 'T' && b.size() <= TextLimit && id != "TXXX") {
            std::string t = trim(decode(b.substr(1), enc));
            auto zero = t.find('\0');
            if (zero != std::string::npos)
              t.resize(zero);
            if (id == "TIT2")
              m.title = t;
            else if (id == "TPE1")
              m.artist = t;
            else if (id == "TALB")
              m.album = t;
            else if (id == "TPE2")
              m.albumArtist = t;
            else if (id == "TYER" || id == "TDRC")
              m.year = t;
            else if (id == "TRCK")
              m.track = std::max(0, atoi(t.c_str()));
            else if (id == "TPOS")
              m.disc = std::max(0, atoi(t.c_str()));
          } else if (id == "APIC" && b.size() <= CoverLimit + 1024) {
            size_t mimeEnd = b.find('\0', 1);
            if (mimeEnd == std::string::npos || mimeEnd + 2 > b.size())
              continue;
            std::string mime = b.substr(1, mimeEnd - 1);
            unsigned type = (unsigned char)b[mimeEnd + 1];
            size_t start = terminated(b, mimeEnd + 2, enc);
            if (start >= b.size() || b.size() - start > CoverLimit ||
                (mime != "image/png" && mime != "image/jpeg"))
              continue;
            if (m.cover.empty() || type == 3) {
              m.cover.assign(b.begin() + start, b.end());
              m.coverMime = mime;
            }
          } else if (id == "USLT" && b.size() <= TextLimit && b.size() >= 4) {
            size_t start = terminated(b, 4, enc);
            m.rawLyrics = decode(b.substr(start), enc);
          } else if (id == "SYLT" && b.size() <= TextLimit && b.size() >= 6 &&
                     (unsigned char)b[4] == 2) {
            size_t start = terminated(b, 6, enc);
            Lyrics timed;
            while (start < b.size() && timed.lines.size() < CountLimit) {
              size_t end = terminated(b, start, enc);
              size_t step = enc == 1 || enc == 2 ? 2 : 1;
              if (end + 4 > b.size() || end < start + step)
                break;
              std::string line =
                  decode(b.substr(start, end - start - step), enc);
              double sec = be((const unsigned char *)b.data() + end) / 1000.;
              timed.lines.push_back({sec, line});
              start = end + 4;
            }
            if (!timed.lines.empty()) {
              std::stable_sort(timed.lines.begin(), timed.lines.end(),
                               [](const LyricLine &a, const LyricLine &b) {
                                 return a.seconds < b.seconds;
                               });
              m.lyrics = timed;
            }
          }
        }
      }
    }
  }
  if (fseek(f, -128, SEEK_END) == 0) {
    char v[128];
    if (fread(v, 1, 128, f) == 128 && memcmp(v, "TAG", 3) == 0) {
      if (m.title.empty())
        m.title = trim(policy == LegacyEncoding::Latin1
                           ? decode(std::string(v + 3, 30), 0)
                           : legacy(std::string(v + 3, 30)));
      if (m.artist.empty())
        m.artist = trim(policy == LegacyEncoding::Latin1
                            ? decode(std::string(v + 33, 30), 0)
                            : legacy(std::string(v + 33, 30)));
      if (m.album.empty())
        m.album = trim(policy == LegacyEncoding::Latin1
                           ? decode(std::string(v + 63, 30), 0)
                           : legacy(std::string(v + 63, 30)));
      if (m.year.empty())
        m.year = trim(legacy(std::string(v + 93, 4)));
      if (!m.track && v[125] == 0)
        m.track = (unsigned char)v[126];
    }
  }
  fclose(f);
  if (m.lyrics.lines.empty())
    m.lyrics = ParseLyrics(m.rawLyrics);
  return m;
}
std::string ResolveUrl(const std::string &base, const std::string &ref) {
  std::string r = trim(ref);
  if (r.empty())
    return {};
  if (http(r))
    return encodeUrl(r);
  auto colon = r.find(':');
  auto delimiter = r.find_first_of("/?#");
  if (colon != std::string::npos &&
      (delimiter == std::string::npos || colon < delimiter))
    return {};
  if (!http(base))
    return {};
  auto schemeEnd = base.find("://");
  if (r.compare(0, 2, "//") == 0)
    return encodeUrl(base.substr(0, schemeEnd + 1) + r);
  auto slash = base.find('/', schemeEnd + 3);
  auto q = base.find_first_of("?#", schemeEnd + 3);
  size_t hostEnd = std::min(slash == std::string::npos ? base.size() : slash,
                            q == std::string::npos ? base.size() : q);
  std::string origin = base.substr(0, hostEnd),
              path = hostEnd < base.size() ? base.substr(hostEnd) : "/";
  if (path[0] != '/')
    path = "/" + path; // ensure path begins with slash
  if (r[0] == '#')
    return encodeUrl(base.substr(0, base.find('#')) + r);
  if (r[0] == '?')
    return encodeUrl(base.substr(0, base.find_first_of("?#")) + r);
  path = path.substr(0, path.find_first_of("?#"));
  if (r[0] == '/')
    path = r;
  else
    path = path.substr(0, path.find_last_of('/') + 1) + r;
  auto suffixAt = path.find_first_of("?#");
  std::string suffix =
      suffixAt == std::string::npos ? "" : path.substr(suffixAt);
  path = path.substr(0, suffixAt);
  std::vector<std::string> parts;
  std::istringstream in(path);
  std::string p;
  while (std::getline(in, p, '/')) {
    if (p == "..") {
      if (!parts.empty())
        parts.pop_back();
    } else if (p != "." && !p.empty())
      parts.push_back(p);
  }
  std::string o = origin + "/";
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i)
      o += '/';
    o += parts[i];
  }
  if (path.size() > 1 && path.back() == '/' && o.back() != '/')
    o += '/';
  return encodeUrl(o + suffix);
}
std::vector<Track> ParseCatalog(const std::string &raw, const std::string &base,
                                const std::string &source, std::string &error) {
  std::vector<Track> out;
  error.clear();
  auto fail = [&](const char *e) {
    error = e;
    return std::vector<Track>();
  };
  if (raw.size() > JsonLimit)
    return fail("catalog exceeds 8 MiB");
  std::string text = raw;
  if (text.compare(0, 3, "\xef\xbb\xbf") == 0)
    text.erase(0, 3);
  text = trim(text);
  if (text.empty())
    return fail("empty catalog");
  if (text[0] == '[' || text[0] == '<')
    return fail("expected JSON catalog object or M3U");
  if (source.empty() || source.size() > 1024)
    return fail("invalid source ID");
  std::set<std::string> ids;
  if (text[0] == '{') {
    auto d = json(text);
    if (!d)
      return fail("invalid JSON");
    auto r = yyjson_doc_get_root(d.get());
    auto arr = yyjson_obj_get(r, "tracks");
    if (!validVersion(r) || !yyjson_is_arr(arr) ||
        yyjson_arr_size(arr) > CountLimit)
      return fail("unsupported catalog schema or size");
    size_t i, n;
    yyjson_val *v;
    yyjson_arr_foreach(arr, i, n, v) {
      Track t;
      t.source = SourceKind::Cloud;
      t.sourceId = source;
      auto audio = str(v, "audio");
      t.path = ResolveUrl(base, audio);
      if (!http(t.path) || audio.empty())
        return fail("audio URL must use HTTP(S)");
      auto id = str(v, "id");
      t.id = "cloud:" + source + ":" + (id.empty() ? hash(t.path) : id);
      if (!ids.insert(t.id).second)
        return fail("duplicate track ID");
      t.title = str(v, "title");
      t.artist = str(v, "artist");
      t.album = str(v, "album");
      if (t.album.empty())
        t.album = "未知专辑";
      t.duration = std::max(0., num(v, "duration"));
      t.cover = ResolveUrl(base, str(v, "cover"));
      t.lyrics = ResolveUrl(base, str(v, "lyrics"));
      if (t.title.empty())
        t.title = audio.substr(audio.find_last_of('/') + 1);
      out.push_back(t);
    }
    return out;
  }
  if (text.find("#EXT-X-") != std::string::npos)
    return fail("HLS is unsupported");
  bool recognizable = text.compare(0, 7, "#EXTM3U") == 0;
  std::istringstream lines(legacy(text));
  std::string line, title, artist;
  double duration = 0;
  Track imported;
  bool hasImported = false;
  while (std::getline(lines, line)) {
    line = trim(line);
    if (line.empty())
      continue;
    if (line.compare(0, 12, "#PAPA-TRACK ") == 0) {
      auto d = json(line.substr(12));
      if (d) {
        imported = trackRead(yyjson_doc_get_root(d.get()));
        hasImported = !imported.path.empty();
      }
      continue;
    }
    if (line.compare(0, 8, "#EXTINF:") == 0) {
      auto comma = line.find(',');
      if (comma != std::string::npos) {
        duration = std::max(0., atof(line.substr(8, comma - 8).c_str()));
        title = line.substr(comma + 1);
        auto dash = title.find(" - ");
        artist.clear();
        if (dash != std::string::npos) {
          artist = title.substr(0, dash);
          title = title.substr(dash + 3);
        }
      }
      recognizable = true;
      continue;
    }
    if (line[0] == '#')
      continue;
    if (out.size() >= CountLimit)
      return fail("too many tracks");
    Track t;
    if (hasImported && imported.path == line) {
      t = imported;
      hasImported = false;
    } else {
      t.path = ResolveUrl(base, line);
      if (!http(t.path) || (line.find(':') != std::string::npos && !http(line)))
        return fail("unsupported playlist URL");
      t.id = "cloud:" + source + ":" + hash(t.path);
      t.sourceId = source;
      t.source = SourceKind::Cloud;
      t.title = title.empty() ? line.substr(line.find_last_of('/') + 1) : title;
      t.artist = artist;
      t.duration = duration;
    }
    out.push_back(t);
    title.clear();
    artist.clear();
    duration = 0;
    recognizable = true;
  }
  if (!recognizable)
    return fail("unrecognized catalog");
  return out;
}
void EnrichTrack(Track &t, const std::string &cache) {
  if (t.source == SourceKind::Cloud || http(t.path))
    return;
  FILE *f = openFile(t.path, "rb");
  if (!f)
    return;
  std::string sample;
  char b[65536];
  size_t n = fread(b, 1, sizeof b, f);
  sample.append(b, n);
  long size = 0;
  if (fseek(f, 0, SEEK_END) == 0)
    size = ftell(f);
  if (size > long(sizeof b) &&
      fseek(f, std::max(0L, size - long(sizeof b)), SEEK_SET) == 0) {
    n = fread(b, 1, sizeof b, f);
    sample.append(b, n);
  }
  fclose(f);
  sample += ":" + std::to_string(size);
  if (t.id.empty())
    t.id = "sd:" + hash(sample);
  t.sourceId = "sd";
  auto m = ReadMetadata(t.path);
  if (!m.title.empty())
    t.title = m.title;
  if (!m.artist.empty())
    t.artist = m.artist;
  if (!m.album.empty())
    t.album = m.album;
  t.albumArtist = m.albumArtist;
  t.trackNumber = m.track;
  t.discNumber = m.disc;
  std::string stem = t.path.substr(0, t.path.find_last_of('.'));
  std::string side;
  if (t.lyrics.empty() && readFile(stem + ".lrc", side, TextLimit))
    t.lyrics = stem + ".lrc";
  std::string key = hash(t.id);
  if (t.cover.empty() && !m.cover.empty() && !cache.empty()) {
    std::string p =
        cache + "/" + key + (m.coverMime == "image/png" ? ".png" : ".jpg");
    if (writeFile(p, std::string(m.cover.begin(), m.cover.end())))
      t.cover = p;
  }
  if (t.cover.empty()) {
    auto slash = t.path.find_last_of("/\\");
    std::string dir =
        slash == std::string::npos ? "" : t.path.substr(0, slash + 1);
    for (const char *name :
         {"cover.jpg", "folder.jpg", "cover.png", "folder.png"}) {
      FILE *c = openFile(dir + name, "rb");
      if (c) {
        fclose(c);
        t.cover = dir + name;
        break;
      }
    }
  }
  if (t.lyrics.empty() && !cache.empty() &&
      (!m.rawLyrics.empty() || !m.lyrics.lines.empty())) {
    std::string p = cache + "/" + key + ".lrc", text = m.rawLyrics;
    if (!m.lyrics.lines.empty()) {
      text.clear();
      for (const auto &l : m.lyrics.lines) {
        char ts[64];
        snprintf(ts, sizeof ts, "[%02d:%05.2f]", int(l.seconds / 60),
                 fmod(l.seconds, 60));
        text += ts + l.text + "\n";
      }
    }
    if (writeFile(p, text))
      t.lyrics = p;
  }
}
bool MatchesTrack(const Track &t, const std::string &query) {
  auto lower = [](std::string s) {
    for (char &c : s)
      if (c >= 'A' && c <= 'Z')
        c += 32;
    return s;
  };
  std::string q = lower(trim(query));
  if (q.empty())
    return true;
  for (const auto &field : {t.title, t.artist, t.album, t.albumArtist}) {
    if (lower(field).find(q) != std::string::npos)
      return true;
    std::string initials;
    bool ok = true;
    for (size_t i = 0; i < field.size();) {
      auto c = nextUtf(field, i, ok);
      if (c >= 0x3400 && c < 0xa000)
        initials += tables::initials[c - 0x3400];
      else if (c < 128 && !std::isspace(int(c)))
        initials += char(c);
    }
    if (lower(initials).find(q) != std::string::npos)
      return true;
  }
  return false;
}
bool SaveMediaState(const std::string &p, const MediaState &s, std::string *e) {
  if (s.sources.size() > CountLimit || s.favorites.size() > CountLimit ||
      s.playlists.size() > CountLimit || s.lyricsOffsets.size() > CountLimit) {
    if (e)
      *e = "state count exceeded";
    return false;
  }
  std::string o = "{\"version\":1,\"sources\":" + sourceJson(s.sources) +
                  ",\"favorites\":[";
  bool comma = false;
  for (const auto &id : s.favorites) {
    if (comma)
      o += ',';
    comma = true;
    o += quote(id);
  }
  o += "],\"playlists\":[";
  comma = false;
  for (const auto &pl : s.playlists) {
    if (pl.tracks.size() > CountLimit) {
      if (e)
        *e = "playlist count exceeded";
      return false;
    }
    if (comma)
      o += ',';
    comma = true;
    o += "{\"id\":" + quote(pl.id) + ",\"name\":" + quote(pl.name) +
         ",\"tracks\":[";
    for (size_t i = 0; i < pl.tracks.size(); ++i) {
      if (i)
        o += ',';
      o += trackJson(pl.tracks[i]);
    }
    o += "]}";
    if (o.size() > JsonLimit) {
      if (e)
        *e = "state size exceeded";
      return false;
    }
  }
  o += "],\"lyricsOffsets\":{";
  comma = false;
  for (const auto &v : s.lyricsOffsets) {
    if (comma)
      o += ',';
    comma = true;
    o += quote(v.first) + ":" + number(v.second);
  }
  o += "},\"resume\":{\"track\":" + trackJson(s.resume.track) +
       ",\"position\":" + number(std::max(0., s.resume.position)) +
       ",\"mode\":" + number(int(s.resume.mode)) +
       ",\"volume\":" + number(std::max(0, std::min(100, s.resume.volume))) +
       ",\"valid\":" + (s.resume.valid ? "true" : "false") + "}}";
  return atomicSave(p, o, e, "favorites");
}
bool LoadMediaState(const std::string &p, MediaState &s, std::string *e) {
  std::string text;
  if (!recoverRead(p, text, "favorites")) {
    if (e)
      *e = "state unavailable or invalid";
    return false;
  }
  auto d = json(text);
  auto r = yyjson_doc_get_root(d.get());
  MediaState out;
  sourceRead(r, out.sources);
  auto a = yyjson_obj_get(r, "favorites");
  size_t i, n;
  yyjson_val *v;
  yyjson_arr_foreach(a, i, n, v) {
    if (i >= CountLimit)
      break;
    if (yyjson_is_str(v))
      out.favorites.insert(yyjson_get_str(v));
  }
  a = yyjson_obj_get(r, "playlists");
  yyjson_arr_foreach(a, i, n, v) {
    if (i >= CountLimit)
      break;
    MediaPlaylist pl;
    pl.id = str(v, "id");
    pl.name = str(v, "name");
    auto tracks = yyjson_obj_get(v, "tracks");
    size_t j, count;
    yyjson_val *x;
    yyjson_arr_foreach(tracks, j, count, x) {
      if (j >= CountLimit)
        break;
      pl.tracks.push_back(trackRead(x));
    }
    out.playlists.push_back(pl);
  }
  a = yyjson_obj_get(r, "lyricsOffsets");
  yyjson_val *k;
  yyjson_obj_foreach(a, i, n, k, v) {
    if (i >= CountLimit)
      break;
    if (yyjson_is_num(v))
      out.lyricsOffsets[yyjson_get_str(k)] = yyjson_get_num(v);
  }
  auto resume = yyjson_obj_get(r, "resume");
  out.resume.track = trackRead(yyjson_obj_get(resume, "track"));
  out.resume.position = std::max(0., num(resume, "position"));
  out.resume.mode = Mode(std::max(0, std::min(3, integer(resume, "mode", 3))));
  out.resume.volume =
      std::max(0, std::min(100, integer(resume, "volume", 100)));
  out.resume.valid = yyjson_get_bool(yyjson_obj_get(resume, "valid"));
  s = std::move(out);
  if (e)
    e->clear();
  return true;
}
bool LoadSources(const std::string &p, std::vector<MediaSource> &s,
                 std::string *e) {
  std::string text;
  if (!recoverRead(p, text, "sources")) {
    if (e)
      *e = "sources unavailable or invalid";
    return false;
  }
  auto d = json(text);
  std::vector<MediaSource> out;
  sourceRead(yyjson_doc_get_root(d.get()), out);
  s = std::move(out);
  if (e)
    e->clear();
  return true;
}
bool SaveSources(const std::string &p, const std::vector<MediaSource> &s,
                 std::string *e) {
  if (s.size() > CountLimit) {
    if (e)
      *e = "too many sources";
    return false;
  }
  return atomicSave(p, "{\"version\":1,\"sources\":" + sourceJson(s) + "}", e,
                    "sources");
}
bool LoadOverrides(const std::string &p,
                   std::map<std::string, MediaOverride> &out, std::string *e) {
  std::string text;
  if (!recoverRead(p, text, "tracks", true)) {
    if (e)
      *e = "overrides unavailable or invalid";
    return false;
  }
  auto d = json(text);
  auto a = yyjson_obj_get(yyjson_doc_get_root(d.get()), "tracks");
  if (!yyjson_is_obj(a)) {
    if (e)
      *e = "invalid overrides";
    return false;
  }
  std::map<std::string, MediaOverride> result;
  size_t i, n;
  yyjson_val *k, *v;
  yyjson_obj_foreach(a, i, n, k, v) {
    if (i >= CountLimit) {
      if (e)
        *e = "too many overrides";
      return false;
    }
    result[yyjson_get_str(k)] = {str(v, "title"), str(v, "artist"),
                                 str(v, "album"), str(v, "cover"),
                                 str(v, "lyrics")};
  }
  out = std::move(result);
  if (e)
    e->clear();
  return true;
}
void ApplyOverride(Track &t, const MediaOverride &v) {
  if (!v.title.empty())
    t.title = v.title;
  if (!v.artist.empty())
    t.artist = v.artist;
  if (!v.album.empty())
    t.album = v.album;
  if (!v.cover.empty())
    t.cover = v.cover;
  if (!v.lyrics.empty())
    t.lyrics = v.lyrics;
}
std::string ExportPlaylist(const MediaPlaylist &p) {
  std::string o = "#EXTM3U\n# Local SD paths require the same device/mount.\n";
  for (const auto &t : p.tracks) {
    if (t.path.find_first_of("\r\n") != std::string::npos)
      continue;
    o += "#PAPA-TRACK " + trackJson(t) +
         "\n#EXTINF:" + number(t.duration > 0 ? t.duration : -1) + ",";
    std::string label = t.artist + " - " + t.title;
    for (char &c : label)
      if (c == '\r' || c == '\n')
        c = ' ';
    o += label + "\n" + t.path + "\n";
    if (o.size() > JsonLimit)
      return {};
  }
  return o;
}
} // namespace papa
