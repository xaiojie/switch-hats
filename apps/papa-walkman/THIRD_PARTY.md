# Third-party source

- `third_party/minimp3/`: [lieff/minimp3](https://github.com/lieff/minimp3), CC0/public domain, original notice retained in headers. The extended decoder header has a local configuration guard for streaming I/O size.
- `third_party/stb/`: [nothings/stb](https://github.com/nothings/stb), dual public-domain / MIT, see bundled `LICENSE`. The app uses `stb_truetype` and `stb_image`; `stb_image_write` is used only for host preview images.
- `third_party/yyjson/`: [ibireme/yyjson](https://github.com/ibireme/yyjson), MIT, see bundled `LICENSE`.
- `source/media_tables.hpp`: generated GBK decoding and pinyin initial tables. The pinyin data is derived from pypinyin 0.55.0; its MIT notice is retained in that header.
- libcurl and zlib are provided by the external devkitPro toolchain; their source is not vendored here. libcurl on Switch uses the libnx SSL backend.
- `tests/tone.mp3`: original two-second 440Hz synthesized sine wave generated with LAME via `lameenc`. Dedicated to the public domain under CC0; no third-party music.
- `assets/icon-master.png`: generated specifically for this application using Image Gen. `icon.jpg` is the menu-sized export.

Nintendo system fonts are accessed at runtime through libnx and are not distributed in this project or release.
