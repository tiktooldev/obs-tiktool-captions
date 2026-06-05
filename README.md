# TikTool Live Captions for OBS

Production-grade real-time captions for TikTok LIVE, packaged as a native OBS plugin. Vertical glassmorphic dock, low CPU + GPU footprint, first-run wizard, drop-in 9:16 browser-source overlay, 10 free minutes per IP (5 if translating), Google sign-in for another 10 minutes, in-OBS Stripe top-ups.

> Built on top of the same caption engine that powers https://tik.tools/captions/watch and the existing overlay at https://overlay.tik.tools. The plugin streams your OBS audio straight to the TikTool relay; the relay shares one Soniox session across the dock + your browser overlay so you are billed once for both surfaces.

## Why this exists

TikTok LIVE creators need captions that:
- Look great on a vertical 9:16 stream right out of the box.
- Don't fight their OBS audio chain (no extra virtual mic).
- Translate live to any of 12 languages.
- Bill predictably (one credit per minute of source, plus one per minute of translation).
- Just work for non-technical users on the first install.

This plugin delivers exactly that, with a first-run wizard that takes under a minute.

## Features

- **Native OBS dockable widget** built with Qt6, glassmorphic theme, vertical compact layout.
- **First-run wizard** picks the mic, language, installs the overlay, runs a 30 s demo - zero typing required.
- **Audio is taken from an OBS source you select**, not a separate device. Whatever mix you're already broadcasting is what gets transcribed.
- **Direct PCM16 mono 16 kHz** stream to `wss://api.tik.tools/captions/ws`. No extra audio driver. No virtual sound card. Resampler is a lightweight linear interpolator - sub-1% CPU on a 2020 mid-range laptop in tests.
- **Smart memory hygiene**: bounded audio backlog, transcript trimmed at 240 paragraphs, lazy WebSocket reconnect with exponential back-off, all OBS sources released on unbind.
- **One-click vertical overlay** (1080x1920 browser source named "TikTool Captions") into your current scene.
- **Live transcript** rendered inside the dock so you see what your viewers will see.
- **Trial gating without an account**: 10 free minutes per IP, 5 if translating. Hit the wall, click "Sign in with tik.tools", grab another 10 for free.
- **In-OBS topups** route to the same Stripe Checkout the website uses, so cards on file just work.
- **Real-time activate / deactivate**: changes in the dock sync to the cloud via ClickHouse + Dragonfly so the same setup is available across machines.
- **Cross-platform**: Windows x64, macOS universal, Linux x86_64 - one CMake graph.

## Quick start (streamer)

1. Install the plugin (see [INSTALL.md](INSTALL.md)).
2. Restart OBS. Open `View -> Docks -> TikTool Live Captions`.
3. The wizard pops up. Pick your mic, language, hit Next, Next, Done.
4. Click `Start captions`. Your overlay updates in the canvas in real time.
5. Out of minutes? Click `Sign in with tik.tools` (browser opens). After login the plugin picks up your account silently. Keep streaming.

## How the audio path works

```
+----------------+        +----------------+        +----------------------+
|  OBS audio mix |  PCM   |  This plugin   |  WS    |  api.tik.tools relay |
| (mic + scene)  +-------->  resample 16k  +-------->  Soniox + translator |
+----------------+        +----------------+        +----------+-----------+
                                                                |
                                                                | WS subscribers
                                                                v
                                                       +------------------+
                                                       |  Browser overlay |
                                                       |  in OBS scene    |
                                                       +------------------+
```

The plugin does the resampling locally, sends 60 ms PCM16 mono frames to the relay, and the relay fans out to every subscriber (this plugin + the browser overlay in your scene). One Soniox session, two screens. You are billed once.

## Pricing

| Plan        | Minutes | Translation included |
|-------------|---------|----------------------|
| Anonymous trial (per IP) | 10 free | Yes (counts 2x) |
| Account trial (after sign-in)| +10 free | Yes (counts 2x) |
| Topup packs | 1,000 to 20,000 minutes | Yes (counts 2x)|
| Subscription plans | unlimited within tier cap | Yes (counts 2x)|

Top up directly from the dock - same Stripe Checkout the website uses.

## Build from source

See platform-specific scripts:
- Windows: `ci/build-windows.ps1`
- macOS:   `ci/build-macos.sh`
- Linux:   `ci/build-linux.sh`

All three require an OBS 31.x checkout + Qt 6.8+.

## SEO and discovery keywords (for the OBS forum listing)

obs plugin, tiktok live captions, real-time captions obs, live subtitles for streamers,
vertical captions 9:16, obs overlay captions, ai transcription obs, live translation overlay,
soniox obs plugin, captions streamer plugin, tiktok overlay obs, obs live captions vertical,
broadcast subtitles plugin, real-time translation streaming, captions for live streaming obs,
multilanguage captions obs, voice to caption plugin, low cpu obs captions, glassmorphic obs dock,
tik.tools obs plugin, browser source captions, automatic captions obs, captions plugin obs studio.

## License

MIT. See [LICENSE](LICENSE).
