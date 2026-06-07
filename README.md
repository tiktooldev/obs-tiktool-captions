# TikTok Live Captions for OBS - by TikTools

> **Disclaimer.** This is an independent third-party project by **TikTools**. We are **not affiliated with, endorsed by, sponsored by, or in any way officially connected to TikTok or ByteDance Ltd.** "TikTok" is a trademark of ByteDance Ltd. The name appears in this project's title for discovery purposes only - to help OBS streamers who broadcast on TikTok LIVE find the plugin. All trademarks are the property of their respective owners. If you are looking for an official ByteDance product, this is not it. See https://tik.tools for the TikTools project.

Production-grade real-time captions for TikTok LIVE, packaged as a native OBS plugin. Vertical glassmorphic dock, low CPU + GPU footprint, first-run wizard, drop-in 9:16 browser-source overlay, sign-in-gated 60-minute free trial, then a captions plan from https://tik.tools/captions.

> Built on top of the same caption engine that powers https://tik.tools/captions/watch and the existing overlay at https://overlay.tik.tools. The plugin streams your OBS audio straight to the TikTok API; the API shares one STT session across the dock + your browser overlay so you are billed once for both surfaces.

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
- **One-click vertical overlay** (1080x1920 browser source named "TikTok Captions - by TikTools") into your current scene.
- **Live transcript** rendered inside the dock so you see what your viewers will see.
- **Trial gating without an account**: 10 free minutes per IP, 5 if translating. Hit the wall, click "Sign in with tik.tools", grab another 10 for free.
- **In-OBS topups** route to the same billing Checkout the website uses, so cards on file just work.
- **Real-time activate / deactivate**: changes in the dock sync to the cloud via TikTok cloud so the same setup is available across machines.
- **Cross-platform**: Windows x64, macOS universal, Linux x86_64 - one CMake graph.

## Quick start (streamer)

1. Install the plugin (see [INSTALL.md](INSTALL.md)).
2. Restart OBS. Open `View -> Docks -> TikTok Live Captions - by TikTools`.
3. The wizard pops up. Pick your mic, language, hit Next, Next, Done.
4. Click `Sign in with tik.tools` - browser opens, sign in or sign up, return to OBS.
5. Click `Start captions`. Your overlay updates in the canvas in real time. You get 60 free trial minutes.
6. After the trial ends the captions stop and the dock surfaces the upgrade CTA. Subscribe at https://tik.tools/captions (Casual / Pro / Extreme, weekly or monthly) to keep streaming and remove the watermark.

## How the audio path works

```
+----------------+        +----------------+        +----------------------+
|  OBS audio mix |  PCM   |  This plugin   |  WS    |  api.tik.tools API |
| (mic + scene)  +-------->  resample 16k  +-------->  STT + translator |
+----------------+        +----------------+        +----------+-----------+
                                                                |
                                                                | WS subscribers
                                                                v
                                                       +------------------+
                                                       |  Browser overlay |
                                                       |  in OBS scene    |
                                                       +------------------+
```

The plugin does the resampling locally, sends 60 ms PCM16 mono frames to the API, and the API fans out to every subscriber (this plugin + the browser overlay in your scene). One STT session, two screens. You are billed once.

## Pricing

| Plan | Stream time | Translation | Watermark |
|------|-------------|-------------|-----------|
| Free trial (after tik.tools sign-in) | 60 minutes lifetime | Yes (counts 2x) | On |
| Casual weekly / monthly | 12 h / wk · 60 h / mo | Yes (counts 2x) | Off |
| Pro weekly / monthly | 30 h / wk · 140 h / mo | Yes (counts 2x) | Off |
| Extreme weekly / monthly | 60 h / wk · 260 h / mo | Yes (counts 2x) | Off |
| Topup packs | 1,000 to 20,000 minutes | Yes (counts 2x) | Off |

Live pricing always at https://tik.tools/captions - the plugin's "Pick a plan" button drops the streamer straight there.

Anonymous use is disabled - the plugin requires a tik.tools account to stream a single audio frame. Open the dock, hit Sign in, get 60 free minutes. After the trial the captions stop on the live stream and the dock surfaces the upgrade CTA.

## Privacy

What the plugin sends to api.tik.tools:
- The audio of whichever OBS source you select, streamed live and never recorded.
- Your device identifier (a UUID generated locally on first install).
- Your tik.tools session token after sign-in.

What we never collect:
- Local file paths, microphone names, or any other OBS source metadata beyond display name.
- Anything from OBS sources you do not explicitly select for captions.
- Recordings - audio is processed live by the STT engine and discarded.

The audio path is end-to-end TLS to api.tik.tools. Caption transcripts are returned over the same WebSocket and rendered locally in the dock + the browser-source overlay.

## Build from source

See platform-specific scripts:
- Windows: `ci/build-windows.ps1`
- macOS:   `ci/build-macos.sh`
- Linux:   `ci/build-linux.sh`

All three require an OBS 31.x checkout + Qt 6.8+.

## SEO and discovery keywords (for the OBS forum listing)

obs plugin, tiktok live captions, real-time captions obs, live subtitles for streamers,
vertical captions 9:16, obs overlay captions, ai transcription obs, live translation overlay,
captions streamer plugin, tiktok overlay obs, obs live captions vertical,
broadcast subtitles plugin, real-time translation streaming, captions for live streaming obs,
multilanguage captions obs, voice to caption plugin, low cpu obs captions, glassmorphic obs dock,
tik.tools obs plugin, browser source captions, automatic captions obs, captions plugin obs studio.

## License

MIT. See [LICENSE](LICENSE).
