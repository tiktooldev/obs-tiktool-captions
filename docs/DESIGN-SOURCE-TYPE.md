# Why the overlay is a browser source today + plan for a custom source type

## Question raised by the operator

> Install overlay -- adding a source. Can't we do this programmatically without an actual source layer that always sits on top of everything and all screens? Not a good idea? And why. I think the source should be a custom source with full plugin settings and multiple setups they can create, that reflect the settings on their account similar to https://tik.tools/captions-overlay caption appearance section. All those settings. So their overlay can be controlled from both places. One source of truth.

## What the current install does

`OverlayInstaller::installOrUpdate` calls `obs_source_create("browser_source", ...)` and `obs_scene_add(currentScene, src)`. Result: a 1080x1920 browser source named "TikTok Captions - by TikTools" appears in the active scene, sitting at the top of the z-order because new scene items always land at the top.

The browser source loads `https://overlay.tik.tools/?jwt=...&setup=...&wm=...` (the existing edge renderer). The worker renders the captions in HTML + CSS, the source plugin (obs-browser) blits each frame into the OBS canvas.

Pros: rendering, font loading, language fallbacks, real-time scroll, watermark gate, settings push over SSE - all live in one place that the website already uses. Plugin ships one source create call.

Cons:
1. It sits on top of everything visually until the streamer drags it under their game/camera. New users do not expect this.
2. There is no plugin-owned properties panel - the streamer has to right-click the source and edit URL params manually if they want to change anything outside the dock.
3. The "settings" the streamer sees in the dock are not what the website sees on `tik.tools/captions-overlay`. Two separate stores.
4. obs-browser is not enabled in every OBS install; if it is missing the install just fails.

## What a custom source type would deliver

`obs_register_source_s` lets us declare a new source type (id e.g. `tiktok_captions_overlay`) with:

- **Our own properties panel** (`get_properties`) that lists the same fields as the website's caption-appearance card: font family, font size, font color, BG opacity, position, max lines, line height, shadow, uppercase, alignment, language pair, layout id (= setup id from the website).
- **Our own update handler** (`update`) that re-derives the overlay URL when any property changes.
- **Optional native render** (`video_render`) so we are not 100 % dependent on obs-browser. Native render means drawing the transcript text via `gs_*` graphics calls; that is real work (font atlas, line wrap, RTL support) and probably worth doing only after the browser-backed v1 is validated.

For v2 we can ship a custom source whose `video_render` is delegated to an internally-managed browser source (the source owns + hides the browser child, customer never sees it as a separate scene item). That gives us our properties panel + branded source type while reusing the existing overlay renderer.

## One source of truth for settings

This is the more important fix.

Plan:
1. **Plugin reads the account's overlay setups** on first dock load via `GET tik.tools/api/captions-overlay/my-layouts` (already exists). Each setup carries the full appearance config + a stable id.
2. **Dock shows a "Setup" picker** with all the user's setups. Selecting one loads its config into the local controls.
3. **Edits in the dock** PUT/POST back to `tik.tools/api/captions-overlay/layouts` with the same id. Edits in the website land via the same endpoint.
4. **The edge renderer subscribes to the SSE** that already pushes setting changes per-creator (the watch.vue + captions-overlay path already does this). Same channel name (`overlay:settings:<creator>:<keyHash>`) gets `{type:'settings', settings, ts}` events; the overlay browser source refreshes live.
5. **Overlay URL the plugin installs** points at the same setup id: `https://overlay.tik.tools/@<creator>?t=<token>&setup=<id>`. The token is minted by `/api/captions-overlay/create-link` which already handles the lookup.

After this, every appearance change the streamer makes in OBS instantly reflects on the live stream + on the website, and vice versa. Single source of truth = the website's setup row in TikTok cloud, with the cloud cache for low-latency reads.

## Z-order

The "sits on top of everything" complaint goes away as a side effect of switching to a custom source type, because we control the source's default rendering. But for the current browser source we can mitigate by:
- Installing the source as **disabled** (visibility off) so the streamer turns it on after dragging it where they want.
- Or installing the source at the BOTTOM of the z-order (`obs_sceneitem_set_order(item, OBS_ORDER_MOVE_BOTTOM)`), then the streamer pulls it up.

Neither is great. Custom source type is the real answer.

## Sequencing

- v1 (this week): wire the dock to `GET /api/captions-overlay/my-layouts` so the streamer can pick + sync setups. Install the overlay disabled-by-default to stop the "on top of everything" surprise.
- v2 (next iteration): register `tiktok_captions_overlay` source type, properties panel, internally-managed browser sub-source. Streamer adds it like any other source from Add menu, no scripted install needed.
- v3 (when v2 is stable): replace internal browser sub-source with native `gs_*` render so we ship without depending on obs-browser at all.

## On the "do this without adding a layer" idea

There is no way to render visible captions on the stream without a scene item somewhere. The captions ARE the visual output the viewer sees. The choice is:
- **Source** = added once, lives in the scene tree, full plugin control (custom source type is the right shape for that). Recommended.
- **Filter on an existing source** = OBS filters run on a host source; we would need a filter that doubles as a rendering layer. Doable but feels alien to streamers (filters are not where they expect captions).
- **Output plugin** = inject the captions into the final encoded video bytes. Bypasses OBS preview entirely, no live readback, no scene composition. Not what the operator wants.

So: add a source. Question is which kind, and v1 ships browser-backed, v2 ships custom-typed.
