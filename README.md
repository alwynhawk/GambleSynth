# GambleSynth

Pull the lever, get a synth patch. One button, a keyboard, and an engine that tries very hard
to make sure the sound you land on is worth keeping.

By **HWCDealer**. Version 0.9.0 — pre-release, sent round for feedback.

---

## For anyone I sent this to

**Standalone** — unzip and run `GambleSynth.exe`. No DAW needed. If Windows shows a
"protected your PC" box, that's just because the build isn't code-signed yet: click
*More info* → *Run anyway*.

**VST3** — copy `GambleSynth.vst3` into:

```
C:\Program Files\Common Files\VST3
```

Then rescan plugins in your DAW. It shows up as **GambleSynth** by **HWCDealer**.

### How to use it

- **ROLL** — new random sound. That's the whole product.
- **Keyboard** — click it, or play a MIDI keyboard.
- **CHAOS** — takes the guard rails off. Most rolls are ugly, some are gold.
- **SAVE / LOAD** — SAVE pins the current sound, LOAD cycles through the ones you pinned.
- **< / >** — step back and forward through the last 64 rolls.
- **SEED** — every sound has a 6-digit code. Type someone else's in and hit GO to get
  the exact same sound. Send me the good ones.
- The meter on the right shows output level; it inverts and says CLIP if you're too hot.

### Things I'd like to know

- Any clicks, pops or dropouts (tell me your DAW, sample rate and buffer size).
- Rolls that come out silent, or so loud they pin the meter.
- Whether it gets boring — how many pulls before you stop finding new sounds?
- Seeds of anything you actually liked.

---

## Developer notes

### Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j16
```

JUCE 8.0.4 is fetched automatically by CMake. Targets:

| Target | Output |
| --- | --- |
| `GambleSynth_Standalone` | standalone app |
| `GambleSynth_VST3` | VST3 plugin |
| `RenderTest` | headless test + audition tool (no DAW, no audio device) |

### Hidden dev mode

Type **777** into the seed box and press GO: unlocks 67 live parameter sliders plus the
five HOLD reels (OSC / FILT / ENV / MOD / FX) which keep part of a sound while ROLL
re-rolls the rest. Type 777 again to leave — the locks clear when you do.

### Tests

`RenderTest` doubles as the test suite. Every mode prints PASS/FAIL and exits non-zero on
failure:

```sh
build/RenderTest_artefacts/Release/RenderTest <mode>
```

| Mode | Checks |
| --- | --- |
| *(no args, or a number)* | renders N rolls to `gamble_render.wav` for auditioning |
| `clicktest` | per-stage click scan, voice stealing, roll-cut leftover |
| `fxtest` | every effect against a dry render; fails an effect that does nothing |
| `filtertest` | all 5 filter models × 2/4 pole × LP/BP/HP (24 cases) |
| `monotest` | mono / legato / glide |
| `synctest` | tempo-synced delay + gate against a fake host at 120 and 90 BPM |
| `aliastest` | inharmonic energy with and without 2× oversampling |
| `perftest` | worst-case CPU (16 notes × 7-voice unison + full FX) |
| `locktest` | HOLD reels, output meter, state round-trip |
| `devtest` | dev panel, live edits, undo isolation |
| `varietytest` | 40 pulls: no repeats, no duds, seeds still 1:1 (`chaos` for CHAOS mode) |
| `uishot` | renders the editor to `ui_main.png` / `ui_dev.png` without a display |

### Roadmap

`IDEAS.md`.
