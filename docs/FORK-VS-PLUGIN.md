# Should we fork OBS into "TikTools Studio"?

Short answer: no. Not now. Maybe v3.

## What a fork actually costs

Streamlabs forked obs-studio + replaced the Qt UI with React/Electron. Their org has 100+ engineers and ~7 years of head-start. Even with that team they sit weeks-to-months behind upstream OBS releases; every encoder fix, every TLS bump, every NDI plugin update has to be merged + re-tested.

For a one-engineer + AI workflow, a fork carries the following recurring tax:

| Cost                                  | Frequency           | Time per cycle |
|---------------------------------------|---------------------|----------------|
| Merge upstream OBS release            | ~6-8 weeks          | 2-5 days       |
| Resolve merge conflicts in core       | every upstream      | 4-16 hours     |
| Re-test on Win + Mac + Linux          | every release       | 1-2 days       |
| Code-sign + notarize installers       | every release       | 4-8 hours      |
| Auto-update infrastructure            | one-time + on-call  | weeks to build |
| Customer support for fork-only bugs   | continuous          | hours/week     |

Plus distribution: a 150-200 MB cross-platform installer per OS, signed, on a CDN, with delta updates so the streamer's machine does not re-download the entire shell on every plugin change.

For comparison, this plugin's full re-cut cycle (including DLL build + Schannel TLS bundle + ZIP package) is under 10 minutes once the toolchain is staged.

## What "TikTools Studio" would gain

| Win                                       | Achievable in plugin? |
|-------------------------------------------|------------------------|
| Custom welcome / first-run wizard         | YES - we ship one      |
| One-click vertical canvas + virtual cam   | YES - shipped this iter|
| Branded UI                                | NO - Qt theme is dark, OBS chrome stays |
| Replace scene tree with our own widgets   | NO                      |
| Bundle captions + API + viewer count    | YES via more plugins    |
| Ship pre-configured scene collection      | YES - JSON drop-in      |
| Single .exe download "tiktok studio"     | NO - needs fork or installer wrapper |

The only items in the "NO" column are pure UX surface area. Everything load-bearing about a fork is already deliverable via this plugin + a smart installer.

## Middle path: "TikTools Setup" installer wrapper (v2)

Instead of forking OBS, ship a small Inno Setup / Squirrel installer that:

1. Detects whether OBS Studio is installed. If not, downloads + silently installs the latest official OBS Studio.
2. Drops our plugin DLL into `C:\ProgramData\obs-studio\plugins\obs-tiktok-captions\` automatically.
3. Drops a pre-configured Scene Collection JSON into `%APPDATA%\obs-studio\basic\scenes\` so the streamer launches OBS to a finished vertical layout.
4. Drops a profile JSON setting canvas 1080x1920 + output + 30 fps + audio device.
5. Pins a shortcut named "TikTok Captions - by TikTools" that launches `obs64.exe --profile TikTools --collection TikTools --startvirtualcam`.

Net experience: streamer downloads `tiktok-setup.exe`, double-clicks, opens "TikTok Captions - by TikTools" from the Start menu, and lands in a fully configured OBS + plugin + virtual camera state. Zero clicks inside OBS.

Cost: a few days to build the installer wrapper. Maintenance burden ~equal to maintaining our current plugin distribution because we are not touching OBS internals.

## When a real fork starts making sense

Only when ALL of the following hit:

- We have multiple paid plugins (captions + viewer count + chat + gift overlay) and the unified UX matters more than upstream parity.
- We have 5+ engineers who can absorb the merge tax.
- We have a brand promise the OBS chrome actively gets in the way of (e.g. "no scene tree, no advanced settings, just go live").
- We are eating shame from Streamlabs / Streamerbot in a category where they ship a fork advantage.

Until then: invest in the plugin + the installer wrapper. The ROI of the fork is negative.

## Decision

**v1 (now):** plugin + one-click OBS auto-config inside the plugin (shipped this iteration).
**v2 (next):** "TikTools Setup" .exe wrapper that bootstraps OBS + plugin + scene collection + profile.
**v3 (only if needed):** evaluate fork after we have at least 3 paid plugins + 5 engineers.
