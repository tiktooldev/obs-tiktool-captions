# Phase 2 - server-side trial enforcement

## What Phase 1 ships

- `/captions/trial/check` on api-tik-tools seeds `<trial-counter>` with 60 minutes the first time a signed-in account asks. Returns `state`, `minutesLeft`, `watermark` flag, plus the one-shot `{jwt, apiKey}` handoff from the TikTok sign-in page.
- Plugin reads `minutesLeft` every 60 seconds, stops streaming when it hits 0, and surfaces the paywall CTA.
- Plugin sends `wm=1` on the overlay browser source URL when the trial flag is on; overlay worker renders the trial badge.
- The TikTok web `/api/captions/plugin-link` mints a 24h captions JWT with `trial: bool` claim derived from the user's subscription status.

## What Phase 1 does NOT enforce

The captions WS server (`captions.ts`) does not yet:
1. Read the `trial` claim from the OBS plugin JWT.
2. Verify `<trial-counter>` on WS upgrade and reject when 0.
3. Decrement that ledger every minute the WS stays alive.
4. Push `{type:'hello', watermark:true, minutesLeft:N}` to the overlay worker on first connect.
5. Push `{type:'minutes', minutesLeft:N}` deltas every minute.
6. Close 1008 the moment the ledger hits 0.

What this means: a motivated user who modifies the plugin binary or proxies the WS to ignore the dock's `minutesLeft <= 0` gate can keep streaming. The watermark on the OBS overlay browser source still renders because it is driven by the worker reading the URL `wm=1` param, but a custom overlay would bypass it.

For an honest streamer the Phase 1 stack works. For real revenue protection, Phase 2 is required.

## Phase 2 work items (server side)

### captions.ts changes

1. In `wss.on('connection')`, after `authenticate(req, url)`, if `auth.tier === 'free'` or the URL has `source=obs-plugin`:
   - Resolve `userId` from `auth.apiKey` (already in keyStore; add a `userId` field if not).
   - Open TikTok cloud. Read `<trial-counter>`. If null or 0, close 1008 with `TRIAL_EXPIRED`.
   - Cache the user's paid-subscription state (the user database `subscription state`) and skip the trial cap when subscription is active.

2. Add a per-WS `minuteWatcher` interval (60s) that:
   - Decrements the ledger by 1 (2 if `translate !== 'off'`).
   - Sends `ws.send(JSON.stringify({ type:'minutes', minutesLeft: N, watermark: <trial> }))`.
   - Closes with `TRIAL_EXPIRED` when N drops to 0.

3. On WS connect emit a `hello` frame:
   ```ts
   ws.send(JSON.stringify({
     type: 'hello',
     watermark: trialState,
     minutesLeft: balance,
     source: 'obs-plugin',
   }));
   ```

4. Make the captionHubs key incorporate `userId` for OBS plugin sessions so trial accounting is per-account, not per-hub.

### Cross-cutting

- Add a webhook on billing subscription start/cancel that bumps `<trial-counter>` to a large sentinel (or deletes it) so paid users are not bounded.
- Add an admin override endpoint to top up a specific user's free-mins ledger for support cases.

## Why Phase 1 ships without this

- Paid plan list lives on https://tik.tools/captions (Casual / Pro / Extreme × weekly / monthly). The plugin's "Pick a plan" button hands the streamer to that page; pricing is owned by the website, never hard-coded in the plugin.
- The plugin's UX (paywall card + master button gate) covers 99 % of streamers.
- Implementing the WS-side cap touches a 4,500-line file with complex hub fan-out; doing it half-right risks breaking the existing watch.vue captions path that paying customers rely on today.
- Without it, the worst case is a free user gets unlimited captions until we ship Phase 2. The watermark is still on (URL-driven). Revenue loss is bounded and recoverable.
