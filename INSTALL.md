# Install

Three OSes, two install methods: prebuilt binary (recommended for streamers) or build from source (recommended for contributors).

## Prebuilt binary

Grab the matching archive from the latest release at https://github.com/tiktooldev/obs-tiktok-captions/releases.

### Windows

1. Close OBS.
2. Extract the zip.
3. Copy `obs-tiktok-captions.dll` into `C:\Program Files\obs-studio\obs-plugins\64bit\`.
4. Copy the `data\obs-plugins\obs-tiktok-captions\` folder into `C:\Program Files\obs-studio\data\obs-plugins\`.
5. Start OBS. Open `View -> Docks -> TikTok Live Captions - by TikTools`.

### macOS

1. Close OBS.
2. Unzip the archive.
3. Drag `obs-tiktok-captions.plugin` into `~/Library/Application Support/obs-studio/plugins/`.
4. Start OBS. Open `View -> Docks -> TikTok Live Captions - by TikTools`.

If macOS Gatekeeper complains, right-click the bundle in Finder, choose `Open`, confirm once. After that OBS will load it normally.

### Linux

1. Close OBS.
2. Extract the tar.
3. Copy the tree into `~/.config/obs-studio/plugins/obs-tiktok-captions/` so you end up with:
   ```
   ~/.config/obs-studio/plugins/obs-tiktok-captions/bin/64bit/obs-tiktok-captions.so
   ~/.config/obs-studio/plugins/obs-tiktok-captions/data/locale/en-US.ini
   ~/.config/obs-studio/plugins/obs-tiktok-captions/data/style.qss
   ```
4. Start OBS. Open `View -> Docks -> TikTok Live Captions - by TikTools`.

## First run

When the dock appears the wizard opens automatically. Five short steps. Pick your mic, your language, hit Install Overlay, then Start Captions. The overlay browser source named `TikTok Captions - by TikTools` lands inside your active scene as a 1080x1920 vertical layer. Move and resize freely - the plugin remembers it.

## Build from source

Cross-platform CMake. Pick the matching script under `ci/`. Requires:

- CMake 3.28+
- Qt 6.8+ (Core, Widgets, Gui, Network, WebSockets)
- OBS Studio 31.x source checkout (tag `v31.0.0`)
- A C++20 compiler (MSVC 2022 / clang 16 / gcc 13)

The CMakeLists is self-contained and includes a minimal cmake bootstrap; for production builds you should swap `cmake/common/bootstrap.cmake` for the full `obs-plugintemplate` submodule:

```
git submodule add https://github.com/obsproject/obs-plugintemplate cmake/common
```

That gives you the upstream packaging helpers (signing, notarization on macOS, NSIS on Windows, etc.).

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| Dock missing after install | Plugin DLL/.so/.dylib not in the OBS plugin path | Re-check the install paths above; OBS logs say which dirs it scans |
| Wizard never opens | Already completed once | Click `Re-run wizard` in the dock footer |
| "Could not create browser source" | obs-browser plugin missing | Install obs-browser (ships with OBS 27+ by default) |
| `Connecting...` never turns green | Network filter or proxy blocking WebSockets | Allow outbound to `api.tik.tools` on 443 |
| No audio detected | Picked the wrong OBS source | Pick a source that has the mic input - Mic/Aux usually works |
| Out of minutes immediately | Anonymous IP trial already used | Sign in for 10 more, or top up |
