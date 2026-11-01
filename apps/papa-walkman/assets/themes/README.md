# Theme artwork

`vinyl.png` is original generated artwork for the Night Record theme. The source PNG is embedded unchanged as `source/ui_vinyl.inc`; its alpha channel is preserved. The renderer decodes it once at a maximum edge of 384 pixels and reuses the decoded image. Album covers continue to come from the selected track.

The illustrative street photograph in `docs/design/2026-10-30-player-redesign/demo-cover.png` is preview-only and is not embedded in the application.

To regenerate the byte array after an intentional asset replacement, encode the exact PNG bytes into `static const unsigned char kVinylPng[]`. Do not add file I/O or image decoding to the frame loop.
