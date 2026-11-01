# Three native player themes

User approved all three displayed mockups and explicitly requested switching, development and compilation. This replaces the previous choose-one gate. Implement directly in the native app, using existing playback, cover, lyrics and threading infrastructure. Keep the verified 0.4.0 backup unchanged.

Targets in docs/design/2026-10-30-player-redesign: 01-album-liner.png, 02-night-record.png, 03-lyric-theatre.png. Theme AlbumLiner=0: ivory, large left sleeve/right metadata and lyric excerpt. NightRecord=1: charcoal, central sleeve with vinyl behind, left metadata/right lyrics. LyricTheatre=2: butter, left album and metadata, dominant current lyric on the right. Real track data replaces the mockup content. Chinese native system fonts are retained for coverage/performance; typography/spacing/hierarchy follow the supplied landing-page-design skill adapted to native 1280×720.

All three primary entries and child pages use matching palette, readable text, selected and pressed states. Settings has a large CycleTheme button replacing the old tape preference control; press right stick on Settings for the same action. Default AlbumLiner. Theme switches immediately, persists through asynchronous settings save, does not reopen audio, change queue or alter song identity. Unknown/missing theme values fall back to AlbumLiner. Existing settings files remain compatible. Active touch gestures cancel if theme/layout changes.

Player drawing, hit targets and seek fractions share one geometry definition per theme. Long titles/lyrics and missing cover/plain/missing lyrics must fit. Real-time visualizations are not required; static vinyl artwork is decoded once. Input and metadata behavior remains intact.

Version 0.5.0 in CMake project/NACP and log constants. User currently requests code/build only; Switch unavailable. Host tests and native renderer previews are evidence for local behavior, not hardware FPS. No FTP deployment, no title/install changes. Preserve inherited dirty edits and do not auto-commit.
