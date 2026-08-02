## For anyone I sent this to

**Standalone** unzip and run `GambleSynth.exe`. If Windows shows a
"protected your PC" box, that's just because the build isn't code-signed yet: click
*More info* then *Run anyway*.

**VST3** copy `GambleSynth.vst3` into:

```
C:\Program Files\Common Files\VST3
```
### Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j16
```
### dev mode

Type **777** into the seed box and press GO:
### Tests

`RenderTest` doubles as the test suite. Every mode prints PASS/FAIL and exits non-zero on
failure:

```sh
build/RenderTest_artefacts/Release/RenderTest <mode>
```

