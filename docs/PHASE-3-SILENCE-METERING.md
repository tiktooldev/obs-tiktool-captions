# Phase 3 - silence-aware credit metering

## The problem

The streamer's flow is:
1. OBS open, captions Start.
2. Streamer uses TikTok LIVE Studio for the actual broadcast. OBS is just a virtual camera + their captions.
3. Stream goes live -> mic active -> captions billed correctly.
4. Stream ends. Streamer closes TikTok LIVE Studio. OBS stays open. Mic is still hot.
5. Streamer forgets to hit Stop captions. WS stays connected. the STT engine is still billed at 1 credit per minute even though there is silence.
6. Next session: streamer goes live again. OBS captions are still Active. Hopefully transcribes.

What we want:
- Idle periods (mic open, nobody speaking) should NOT decrement the streamer's caption credits.
- Captions should keep streaming responsively the moment speech resumes - no manual restart.
- Plugin auto-resumes on OBS open if streaming was on before quit (already shipped in v1.2 - see captions-dock.cpp).

## What server already does

- captions.ts maintains a per-WS per-minute decrement on the per-key `the caption credits record` row. Hits the STT engine the whole time the WS is open.
- No VAD (voice activity detection) anywhere in the pipeline. the STT engine does its own VAD but charges for the call regardless.

## What Phase 3 adds

A silence detector in captions.ts that:

1. Computes RMS of incoming PCM frames (we already have them in `mode=client` mode).
2. Tracks `silentMs` = milliseconds since last frame whose RMS exceeded a threshold (suggest `-45 dBFS`).
3. When `silentMs > IDLE_GRACE_MS` (suggest 15 s):
   - Stop forwarding audio to the STT engine (drop frames or close that STT session).
   - Stop the per-minute credit decrement for this WS.
   - Push `{type:'idle'}` so the dock can show an "Idle - listening for speech" indicator instead of "Live".
4. When a frame arrives whose RMS > threshold AND we are currently idle:
   - Re-open the STT session (warm-start, no transcript flush).
   - Resume the per-minute decrement.
   - Push `{type:'live'}`.

## Why we need it server-side, not client-side

The plugin could just skip sending silent frames. But:
- A motivated bypass could send fabricated silence frames + receive transcripts of speech the mic IS picking up locally. Trust boundary belongs server-side.
- If the plugin breaks (crash, frozen, accidentally muted at the OS level), the server still sees no audio reaching it and stops billing.
- the STT engine does not refund based on "I sent you silence" - we have to NOT send it the silence in the first place to save the cost.

## Implementation outline (captions.ts)

Inside the WS connection handler:

```ts
let lastNonSilentTs = Date.now();
let idle = false;
const IDLE_GRACE_MS = 15_000;
const RMS_THRESHOLD = 0.0056;   // ~-45 dBFS for int16 PCM

ws.on('message', (raw, isBinary) => {
  if (!isBinary) { /* JSON control frame, untouched */ return; }
  const pcm = new Int16Array(raw.buffer, raw.byteOffset, raw.byteLength / 2);
  let sumSq = 0;
  for (let i = 0; i < pcm.length; ++i) sumSq += pcm[i] * pcm[i];
  const rms = Math.sqrt(sumSq / pcm.length) / 32768;

  if (rms > RMS_THRESHOLD) {
    lastNonSilentTs = Date.now();
    if (idle) {
      idle = false;
      ensureStt(state).resume();
      ws.send(JSON.stringify({ type: 'live' }));
    }
    forwardToStt(state, raw);
  } else {
    if (!idle && Date.now() - lastNonSilentTs > IDLE_GRACE_MS) {
      idle = true;
      ensureStt(state).pause();
      ws.send(JSON.stringify({ type: 'idle' }));
    }
    // While idle we drop frames entirely.
  }
});

// Per-minute decrement runs only while !idle.
const billTimer = setInterval(() => {
  if (idle) return;
  decrementCredits(state, /*minutes=*/1, /*translateBurn=*/state.translate ? 2 : 1);
}, 60_000);
```

The the STT engine `pause` / `resume` semantics depend on whether the API uses
short-lived sessions (re-open on demand) or a long-lived one (we have to
mute upstream). Either path is doable; long-lived is cheaper because the
warm session avoids the re-handshake latency on resume.

## Plugin-side UI for the idle state

When the dock receives `{type:'idle'}` from the WS:
- Status pill: "Idle - listening for speech" (variant: 'idle').
- Level meter still shows real input level, but in muted color.
- Minute counter does NOT decrement.

When `{type:'live'}`:
- Pill back to "Live", normal color.

This is a ~10-line change in captions-dock.cpp's `onTextPayload` once the
server starts pushing the events. The plugin handles it gracefully even
if the server never sends them (existing minute-decrement-by-poll path
just keeps running, exactly today's behavior).

## Tuning the RMS threshold

`-45 dBFS` catches typical room tone + breath noise without false-positives on actual quiet speech. Adjust per the STT engine own VAD sensitivity to avoid overlapping decisions:
- the STT engine already sends `{type:'speech_started'}` events; if we receive one within the IDLE_GRACE_MS window we should also re-arm the live state.
- Conversely if the STT engine sends `{type:'speech_ended'}` and we have been below threshold for >5 s we can collapse straight into idle without waiting for the full grace window.

## What this does NOT do

- It does not detect "TikTok LIVE Studio is offline". We have no signal for that. We approximate it via silence detection on the mic which is good enough for the cost-saving angle.
- It does not refund minutes already billed in the previous (pre-Phase-3) regime.
- It does not change the trial seed (60 mins per account) - that is unrelated and lives in `/captions/trial/check`.
