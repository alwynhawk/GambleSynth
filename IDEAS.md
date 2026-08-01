# GambleSynth — Feature Roadmap / Ideas

Ranked by impact for *this* product: a novelty, impulse-buy ($5), social-media-marketed
random-sound synth. The magic is "pull the lever → surprise sound", with a CHAOS mode for
wild experimental gems.

Current state: own JUCE engine (16-voice poly + mono/legato, 3 polyBLEP osc with unison/sub/noise,
5 filter models (SVF/ladder/diode/formant/comb), dual ADSR, LFO, 4 osc modes normal/ring/sync/FM,
stereo; FX rack of drive/fold/crush/phaser/flanger/chorus/delay/reverb/compressor/DC-block/limiter),
archetype-constrained ROLL + 18 modifiers + wide-open CHAOS toggle.

---

## 🎯 Tier 1 — turn the toy into a keeper  ← DONE ✅
Solves the "found a gem but the next ROLL wiped it" problem.

- [x] **Roll history + undo/redo** — `<` / `>` step through the last 64 rolls (ring buffer of patches).
- [x] **Save / favourite a sound** — SAVE pins the current patch; LOAD (n) cycles the bank.
- [x] **Save plugin state** — get/setStateInformation serialises full patch + favourites (round-trip verified).
- [x] **Seed chooser** — every roll shows a 6-digit SEED; type one + GO to regenerate it. (reproducibility verified)

UI for these is functional/plain — proper visual design still to come.

## 🎛️ Tier 2 — the slot-machine hook (leans into the plinko theme)
- [x] **Lockable reels** — five locks (OSC / FILT / ENV / MOD / FX) grouped the way a player hears
      a sound, not how the struct is laid out. ROLL keeps the locked groups and re-rolls the rest;
      locks apply *before* the audibility probe (a locked filter over a fresh oscillator can be
      silent), persist in plugin state, and mark the seed with `*` since a hybrid is no longer
      reproducible from the seed alone. **Dev-mode only** (seed 777) — leaving dev mode clears
      them, so a lock can never shape rolls while its button is hidden.
- **Chaos intensity slider** — tame → unhinged, widens the random ranges instead of binary on/off.
- **Nudge / mutate** — "roll a slightly different version of this" (small random deltas).

## 🔊 Tier 3 — engine depth (more sonic range)
Balancing rule: **Tier-A** params (timbral, can't break a sound) roll freely every time within
archetype ranges; **Tier-B** structural params are archetype-gated + occasional. CHAOS ignores gates.

Tier-A batch — DONE ✅ (archetype-gated ranges wired in):
- [x] **Supersaw unison** — 1–7 detuned, stereo-spread voices per osc (pads/stabs lush, bass off).
- [x] **Sub oscillator + noise layer** — sub octave-down (bass big, sine/square), pre-filter noise texture.
- [x] **PWM** — LFO-moved pulse width on the square wave (pads/leads).
- [x] **Velocity → filter/amp** — per-archetype sensitivity (plucks/keys expressive).

Tier-B:
- [x] **Glide/portamento + mono/legato mode** — poly uses the Synthesiser; mono/legato uses a single voice driven by a held-note stack (split at MIDI events). Gated to bass/lead; CHAOS randomizes it. Verified: mono/legato/glide render PASS, no NaNs.
- [x] **More filter flavours** — 5 models (clean SVF, Moog-ish ladder, 303 diode, vowel formant,
      tuned comb) + 2/4-pole, picked from a per-archetype approved set; CHAOS rolls any of them.
      Verified: `RenderTest filtertest` renders every model, all finite, levels in range.

**Tier 3 complete.**

## 🎲 Breaking the archetype grid — in progress
Problem: with 7 archetypes, rolls start reading as "one of seven presets" after a few pulls.

- [x] **No-repeat rule** — a free roll won't land on the last two archetypes or repeat the last
      modifier. Done by *rejecting the seed and drawing another* (up to 12 tries), never by
      altering the sound a seed maps to — seed → sound stays 1:1.
- [x] **Modifiers** — a twist applied after the archetype: octave, gated, swell, sub, air, vowel,
      metal, drift, tape, clang, growl, cathedral, dry. 60% of rolls get one, 18% of those get a
      second non-cancelling twist. The ear latches onto the twist instead of the family
      underneath. Measured: unique combos went 21/40 → ~30/40 per 40 pulls.
- [x] **No silent rolls** — a free roll renders a 1 s offline probe of the sound and redraws (≤8
      tries) if it's inaudible. Explicit seeds are never re-rolled, so seed → sound stays 1:1.
      Chaos duds: 5/40 → 0/40. (The big one was a real bug: ladder band-pass output silence.)
- [ ] **Trait slots** — the structural fix: replace monolithic archetypes with independent slots
      (amp shape / timbre source / filter character / motion / space) plus a compatibility matrix,
      so archetypes become weighted biases over slots rather than moulds. Hundreds of coherent
      combinations instead of 7.
- [ ] **Rarity tiers** — weight rolls common / rare / jackpot and show it on the pull. Ties the
      variety work to the gamble theme and gives people something to screenshot.

Next after that: the real plinko/arcade UI (still a placeholder).

## 🎚️ Tier 4 — FX depth (character)  ← DONE ✅
Chain order: drive → fold → crush → DC block → phaser → flanger → chorus → delay →
reverb → compressor → master limiter. All archetype-gated via `pickFx`, wide open in CHAOS.

- [x] **Real compressor** — stereo-linked peak comp on one macro (threshold + ratio + partial
      make-up), before the limiter so it glues instead of squashing.
- [x] **Bitcrusher / sample-rate reducer** — independent bit depth and sample-hold divisor.
- [x] **Phaser / flanger** — 6-stage all-pass sweep with feedback; short modulated delay with
      feedback. Stereo-offset LFOs.
- [x] **Wavefolder** — sine folder, harmonics that clipping can't produce.
- [x] **DC blocker** (was Tier 6) — folding and ring-mod both leave an offset.
- [x] **Drive is no longer a volume knob** — `tanh(x*d)/tanh(d)` has small-signal gain `d/tanh(d)`
      (~9x at full drive), so drive is now make-up compensated against the always-on floor. This
      alone removed every limiter-pinned roll from the test runs.
- [x] **Deterministic voice phases** — seeded from patch seed + note, so a shared seed now
      reproduces down to the sample instead of only matching parameters.
- [x] **Tempo sync — the narrow version** — delay locks to a note division on 80% of rolls, and
      the `gated` modifier locks to 1/16, 1/8T or 1/8 (it's a rhythmic effect; unsynced it just
      sounds sloppy). Vibrato/PWM/filter LFOs deliberately stay free-running. Host BPM comes from
      the VST3 playhead (Ableton + FL both report it), last-good value cached because FL stops
      reporting while the transport is parked, 120 BPM fallback in the standalone. The patch
      stores the *division*, not seconds, so a shared seed keeps its rhythm at any tempo.

New modifiers from this batch: `crush`, `fold`, `phase`, `flange`, `squash` (18 total).

## 🔧 Dev tools
- [x] **Seed 777 unlocks the dev panel** — a scrollable list of every patch parameter (67 sliders)
      as live controls. Edits use `applyLiveEdit`, which bypasses the roll cut (no muting while
      dragging) and skips history (undo still steps between *rolls*, not slider moves). Session
      only — type 777 again to hide it. Fastest way to tune the engine by ear before changing
      archetype ranges.

## 📢 Tier 5 — built for social (the go-viral angle)
- ~~"Record last roll → WAV"~~ — declined (2026-07-31).
- ~~Factory presets~~ — declined (2026-07-31), rolls are the product.
- **Auto-play preview on roll** — plays a short riff so demo videos need zero keyboard skill.
- **Shareable seed + screenshot** — combine with seed codes for TikTok-ready clips.

## 🧹 Tier 6 — polish / technical (before charging money)
- **Host parameters** — the plugin currently publishes *none*, so nothing is automatable or
  MIDI-learnable in Ableton/FL. Minimum: master, CHAOS, and a ROLL trigger (automating ROLL to
  fire every bar is very on-brand).
- **Cross-platform builds** — Standalone + VST3 on Linux only today. Needs Windows VST3, macOS
  VST3/AU (signing + notarisation), an installer, and a clean `pluginval` pass.
- [x] **Oversampling** — 2x on the voices (sync/FM/ring alias at the source, so upsampling later
      can't help) plus drive and the wavefolder; own 63-tap windowed-sinc decimator, 15 samples of
      reported latency. The bitcrusher deliberately stays at base rate — sample-rate reduction
      *is* aliasing and that grit is the effect. Measured inharmonic energy: hard sync 3.03% ->
      0.86%, FM index 9.84% -> 2.49%, wavefolder 0.40% -> 0.01%. Cost: 3-note chord ~5% of a core;
      worst case (16 notes x 7-voice unison + full FX rack) 23% -> 46% of one core.
      Possible follow-up if that matters: auto-bypass the 2x path for patches with no
      nonlinearity engaged (switchable at the roll cut's silence, so it'd be artefact-free).
- **Preset browser** for favourites (name, tag, delete).
- [x] **Output meter** — stereo peak with decay, sqrt-scaled so quiet material still reads, red
      border on clip (click to clear).

---

### Note on saved-sound stability
Favourites & plugin state store the **full resolved parameters** (not just the seed), so
saved sounds stay identical even if the randomizer algorithm changes later. The seed is kept
as shareable metadata; typing a seed regenerates using the *current* algorithm.
