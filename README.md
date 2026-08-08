# GambleSynth

Pull the lever, get a synth patch.

## Installing

Run the installer and pick what you want: the VST3, the standalone, or both.
Installing over an older version replaces it.

It isn't code-signed yet, so Windows will put up a "protected your PC" box the
first time. Click More info, then Run anyway.

The VST3 goes to `C:\Program Files\Common Files\VST3` and the standalone to
`C:\Program Files\HWCDealer\GambleSynth`. If your DAW doesn't list it, rescan.
It shows up as GambleSynth by HWCDealer.

Rescan after updating too. DAWs remember what a plugin looked like last time,
and if that no longer matches, it can load and make no sound at all. In FL it's
right-click the plugin, Refresh plugin properties.

## Using it

Pull the lever. That's the whole thing.

The reels drop one at a time and take about a second and a half to land, so the
lever locks for two seconds while you watch them. Three of a kind flashes, and
three sevens flash gold.

Play it with the on-screen keys or a MIDI keyboard.

CHAOS takes the guard rails off. Most of those rolls are ugly and a few are
worth keeping.

SAVE keeps the sound you're on and LOAD opens the list. Saved sounds don't live
in the project file, so they're there in every session.

NUDGE gives you the same sound played slightly differently — the filter moves a
bit, envelopes drift, modulation wobbles. Nothing structural changes, so it's
still the patch you had. Press it again to wander further. It costs 1 G, and G
is what the reels pay out: 2 for three fruit, 20 for three sevens. You start
with 5.

`<` and `>` step back through the last 64 rolls. The meter is output level and
goes inverse with CLIP on it if you're too hot.

Every sound is a 6-digit seed. Type one in, press Return, and you get exactly
that sound — so seeds are worth sharing. Send me the good ones.

### In a DAW

Nine parameters are automatable: Master, Chaos, Roll, Seed, Arp, Arp Rate, and
trims for filter, reverb and delay.

Roll triggers on the rising edge, so automating a spike every bar gives you a
new sound every bar. The trims scale whatever the roll came up with instead of
replacing it, which keeps them useful across every patch.

Support and anything else: alwynhawk@gmail.com

## What I'd like to know

Clicks, pops or dropouts, and which DAW, sample rate and buffer size. Rolls that
come out silent, or loud enough to pin the meter. How many pulls it takes before
it stops surprising you. And seeds of anything you liked.

---

## Developer notes

Build:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j16
```

CMake fetches JUCE 8.0.4. The targets are the standalone, the VST3, and
`RenderTest`, which is both the test suite and a way to audition rolls without
opening a DAW. Windows builds and the installer come from CI — see
`.github/workflows/build-windows.yml` and `installer/GambleSynth.iss`. The
version number lives in CMakeLists.txt.

Type 777 in the seed box for dev mode: every patch parameter as a live slider,
the five HOLD reels, and a readout of what the host is actually doing. 777 again
to leave.

### Tests

Each mode prints PASS/FAIL and exits non-zero if it fails.

```sh
build/RenderTest_artefacts/Release/RenderTest <mode>
```

Audio: `clicktest` (discontinuities, voice stealing, roll-cut leftovers),
`fxtest`, `filtertest` (all 24 model/pole/type combinations), `monotest`,
`synctest`, `aliastest`, `wttest`, `addtest` (string, chords, noise colours),
`arptest`.

Behaviour: `locktest`, `libtest`, `paramtest`, `devtest`, `hosttest` (bus
layouts, sample rates, oversized blocks), `fruittest`, `nudgetest`.

Judgement calls: `varietytest` (40 pulls, no repeats, no duds, seeds still map
1:1), `similaritytest`, `perftest`, `cputest` (per-roll cost, ranked, with what
each patch has switched on), `uishot` (renders the editor to a PNG headlessly).

`censustest` is a different kind of tool: it rolls a few hundred patches and
counts how often notable traits actually occur. Useful when a particular sound
keeps turning up and the question is whether that's bad luck or the odds.

Run with no arguments, or a number, to render that many rolls to a wav.

The roadmap is in `IDEAS.md`. Licence terms are in `LICENSE.txt` and the
attributions in `THIRD-PARTY.txt`; both ship with the installer.

Two upstream obligations before selling: JUCE 8 is dual-licensed AGPLv3 or
commercial, so a commercial licence is required to ship this closed-source, and
Steinberg require a signed VST3 licence agreement before a VST3 is published.
