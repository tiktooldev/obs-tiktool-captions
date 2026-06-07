# Roadmap

## v1 (this branch, ready to test)

- [x] Account-gated 60-minute free trial
- [x] Sign-in via tik.tools + post-OAuth handoff
- [x] Audio device enumeration (system + OBS sources)
- [x] 41 supported caption languages (source + translate)
- [x] Watermark on overlay during trial
- [x] Hard paywall card when trial ends
- [x] Anti-abuse: hardware fingerprint + CF-IP/24 subnet dedup
- [x] Per-account demo credit grant (was per-key on website too - fixed)
- [x] WS hardened against malicious frame sizes
- [x] Privacy disclosure in wizard + README

## v1.5 (next iteration - setup sync + realtime)

The web-side captions-overlay editor saves layouts to `the saved overlay list` (JSON array) via POST `/api/captions-overlay/my-layouts`. The plugin already has its own style + language controls. v1.5 unifies them:

- [ ] Dock loads the user's saved layouts on sign-in. Shows them in a "Setup" picker above the style sliders. Each layout carries name + style + language config.
- [ ] Selecting a layout loads it into the dock controls.
- [ ] Edits in the dock save back to the same row (debounced, 750 ms).
- [ ] Real-time: dock opens an EventSource to `/api/captions-overlay/stream?layoutId=<id>` so a change made on the website immediately reflects in OBS.
- [ ] Streamer can copy the overlay URL from the dock footer to use in non-OBS surfaces (eg. browser-source-equivalent in vMix).

The existing captions-overlay endpoints (`my-layouts.get`, `my-layouts.post`, `stream.get`, `create-link.post`) already cover the data model. Wiring is a UI + plumbing task, not a schema change.

## v2 - native OBS source type

The "install overlay" button currently calls `obs_scene_add` on a 1080x1920 browser source. Two problems:
1. It lands at the top of the z-order, surprising new users.
2. The settings panel is whatever obs-browser shows, not our own properties UI.

v2 replaces it with a custom source type registered via `obs_register_source_s` (id `tiktok_captions_overlay`). Properties panel mirrors the captions-overlay editor. Internally we still embed a private browser sub-source so we reuse the overlay renderer.

- [ ] Register source type with `get_properties` listing setup id + style overrides.
- [ ] On scene add the user picks a saved layout or "create new from current".
- [ ] Streamer adds it from Sources +, like any built-in source. The dock's "Install overlay" button becomes a shortcut that picks the active scene and calls the same registration code path.
- [ ] Hidden browser sub-source is owned by our source - never appears separately in the scene tree.

## v3 - native render (drop browser dep)

`video_render` callback draws the transcript with `gs_*` calls. Font atlas + line wrap + RTL handled in C++. Removes the obs-browser dependency entirely so the plugin runs in OBS installs without the browser source plugin enabled.

This is the heaviest item. Worth doing only after v2 ships and we know the streamer UX is right.
