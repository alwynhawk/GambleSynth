# GambleSynth

Pull the lever, get a synth patch.

## Installing

Run **GambleSynth-x.y.z-Windows.exe** and pick what you want: the VST3 for your
DAW, the standalone app, or both. Installing over an older version replaces it —
no need to uninstall first.

The build isn't code-signed yet, so Windows shows a "protected your PC" box on
first run. Click *More info* then *Run anyway*.

Installed to:

- VST3 — `C:\Program Files\Common Files\VST3\GambleSynth.vst3`
- Standalone — `C:\Program Files\HWCDealer\GambleSynth\`

If your DAW doesn't list it, rescan plugins. It appears as **GambleSynth** by
**HWCDealer**.

There's also a portable zip if you'd rather drop the `.vst3` in by hand.

## Using it

- **The lever** — new random sound. That's the whole product.
- **Keyboard** — click it, or play a MIDI keyboard.
- **CHAOS** — takes the guard rails off. Most rolls are ugly, some are gold.
- **SAVE / LOAD** — SAVE keeps the current sound; LOAD opens your saved list.
  Saved sounds live outside the project, so they're there in every session.
- **NUDGE** — in the coin tray. Same sound, played slightly differently: the
  filter shifts a little, envelopes drift, modulation wobbles. Structure stays
  put, so it's still recognisably the patch you had. Press again to drift
  further. Turns a near-miss roll into a keeper, and pairs with HOLD.
- **Nudges are won, not given.** Three matching fruit pay 2, three sevens pay
  20. You start with 5. That's what the reels are for.
- **< / >** — step back and forward through the last 64 rolls.
- **SEED** — every sound has a 6-digit code. Type someone else's in and press
  Return to get the exact same sound. Send me the good ones.
- The meter is output level; it inverts and says CLIP if you're too hot.

### In a DAW

Nine parameters are automatable and MIDI-learnable: Master, Chaos, **Roll**,
Seed, Arp, Arp Rate, and trims for filter, reverb and delay.

Roll fires on the rising edge, so automating a spike each bar rolls a new sound
each bar. The trims scale whatever the roll produced rather than replacing it,
so they stay useful across every sound.

## Feedback I'd like

- Clicks, pops or dropouts — tell me your DAW, sample rate and buffer size.
- Rolls that come out silent, or so loud they pin the meter.
- How many pulls before it stops surprising you.
- Seeds of anything you liked.

---

## Developer notes

### Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j16
```

JUCE 8.0.4 is fetched by CMake. Targets: `GambleSynth_Standalone`,
`GambleSynth_VST3`, and `RenderTest` (headless tests + audition tool).

Windows builds and the installer are produced by CI — see
`.github/workflows/build-windows.yml` and `installer/GambleSynth.iss`. The
version comes from `project(GambleSynth VERSION ...)` in CMakeLists.txt.

### Hidden dev mode

Type **777** into the seed box and press Return: unlocks every patch parameter
as a live slider, plus the five HOLD reels which keep part of a sound while the
lever re-rolls the rest. Type 777 again to leave.

### Tests

`RenderTest` is the test suite. Each mode prints PASS/FAIL and exits non-zero on
failure.

```sh
build/RenderTest_artefacts/Release/RenderTest <mode>
```

| Mode | Checks |
| --- | --- |
| *(no args, or a number)* | renders N rolls to `gamble_render.wav` |
| `clicktest` | per-stage click scan, voice stealing, roll-cut leftover |
| `fxtest` | every effect against a dry render |
| `filtertest` | 5 filter models × 2/4 pole × LP/BP/HP |
| `monotest` | mono / legato / glide |
| `synctest` | tempo-synced delay and gate against a fake host |
| `aliastest` | inharmonic energy with and without 2× oversampling |
| `wttest` | wavetable aliasing against the band-limited saw |
| `addtest` | plucked string, chord unison, noise colours |
| `arptest` | arpeggiator timing, overlap, note release |
| `locktest` | HOLD reels, output meter, state round-trip |
| `libtest` | saved sounds persist across instances |
| `paramtest` | host parameters, roll edge-triggering, state |
| `devtest` | dev panel, live edits, window shape |
| `hosttest` | bus layouts, sample rates, oversized blocks |
| `fruittest` | reel lottery odds |
| `varietytest` | 40 pulls: no repeats, no duds, seeds still 1:1 |
| `similaritytest` | how distinct consecutive rolls actually are |
| `perftest` | worst-case CPU |
| `uishot` | renders the editor to PNG without a display |

### Roadmap

`IDEAS.md`.
