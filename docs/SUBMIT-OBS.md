# Submitting to the OBS plugin library

The OBS plugins library at https://obsproject.com/forum/resources/ is moderated. There is no API for submission; everything goes through their forum.

## What they want (per their submission guide)

1. Plugin works on the **latest stable** OBS release at submission time. Test against the exact version.
2. Native binary per OS you support, signed where possible:
   - Windows: code-signed `.dll`
   - macOS: notarized `.plugin` bundle
   - Linux: `.so` plus `.deb` / `.rpm` / `AppImage` (optional but recommended)
3. Source code available (we are open source under MIT - point them at the GitHub repo).
4. A clear forum thread with:
   - Screenshots (3-6, at least one of the dock + at least one in OBS canvas)
   - A short description
   - Install instructions per OS
   - License + source link

## Submission checklist

- [ ] Build all three OS targets from a clean checkout using `ci/build-*` scripts.
- [ ] Sign Windows DLL with an EV cert.
- [ ] Sign + notarize macOS bundle with Apple Developer ID.
- [ ] Smoke-test on the latest OBS Studio stable on each OS:
  - Dock loads
  - Wizard opens on first run
  - Mic picker enumerates audio sources
  - WS connects to `api.tik.tools`
  - Captions render in dock + overlay
  - Topup browser flow opens
- [ ] Take screenshots at 1920x1080 and 2880x1800 for retina.
- [ ] Create release on GitHub with the artifacts.
- [ ] Open a new Resource at https://obsproject.com/forum/resources/categories/plugins.6/.
- [ ] Wait for moderator approval (typically 1 to 7 days).

## Recommended description copy

```
TikTok Live Captions - by TikTools

Real-time captions for TikTok LIVE, packaged as a native OBS dock. Vertical,
glassmorphic, low CPU + GPU footprint. First-run wizard sets up everything in
under a minute, including a 1080x1920 browser source dropped straight into
your active scene.

- Takes audio from any OBS source you pick (no extra mic driver).
- Sends 60 ms PCM16 mono 16 kHz frames to api.tik.tools.
- Translates live into 12 languages.
- 10 free minutes per IP, plus 10 more after a free sign-in. Top up directly
  from the dock when you need more.
- Smart memory hygiene: bounded backlog, lazy reconnect, transcript trimmed,
  OBS sources released on unbind.

Source: https://github.com/tiktooldev/obs-tiktok-captions
License: MIT
Builds: Windows x64, macOS universal, Linux x86_64
```

## SEO / metadata

OBS resource pages let you set tags. Use these:
`captions`, `tiktok`, `live`, `streaming`, `translation`, `subtitles`,
`overlay`, `dock`, `qt6`, `real-time`, `multilanguage`, `accessibility`.

The Resource title should be:
`TikTok Live Captions - by TikTools - Real-time captions and translation for TikTok LIVE`

## Update cadence

OBS encourages plugin authors to keep up with OBS Studio's release schedule (currently roughly every 6-8 weeks). Re-cut a release within a week of each new OBS stable so the moderators don't move the listing to "outdated".
