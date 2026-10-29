# Third-party source

- `third_party/minimp3/`: [lieff/minimp3](https://github.com/lieff/minimp3), CC0/public domain, original notice retained in headers. Reused unchanged from the repository's previous player.
- `third_party/stb/`: [nothings/stb](https://github.com/nothings/stb), dual public-domain / MIT, see bundled `LICENSE`. `stb_truetype` is used by the app; `stb_image_write` is used only for host preview images.
- `tests/tone.mp3`: original two-second 440Hz synthesized sine wave generated with LAME via `lameenc`. Dedicated to the public domain under CC0; no third-party music.
- `assets/icon-master.png`: generated specifically for this application using Image Gen. `icon.jpg` is the menu-sized export.

Nintendo system fonts are accessed at runtime through libnx and are not distributed in this project or release.
