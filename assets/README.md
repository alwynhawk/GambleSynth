# Skin artwork

Any `*.png` in this folder is compiled into the plugin. Remove them all and the
plugin falls back to the plain drawn UI, so the build never depends on art
existing.

`machine.png` is currently a **placeholder**: a labelled box for every control,
drawn at the exact coordinates in `src/Skin.h`. Replace it with the real
artwork.

## Workflow

1. Draw the machine at **900 x 1200** (change `Skin::ArtWidth` / `ArtHeight` if
   you want another canvas — everything follows from those two numbers).
2. Carve out the holes where controls go: reel window, lever, ROLL, seed
   display, buttons, meter, keyboard.
3. For each hole, marquee-select it in Photoshop and read **X / Y / W / H** off
   the Info panel.
4. Put those four numbers into the matching line in `src/Skin.h`. They are
   artwork pixels — no converting.
5. Save over `assets/machine.png`, rebuild, done.

## Checking the alignment

Type **777** into the seed box. Dev mode outlines every control in green on top
of the artwork, so anything sitting outside its hole is obvious.

To check without opening a DAW:

```sh
build/RenderTest_artefacts/Release/RenderTest uishot
```

writes `ui_main.png` and `ui_dev.png` in the project root.

## Notes

- The window keeps the artwork's aspect ratio, so the machine never stretches.
- Sizes are relative, so the whole UI scales with the window.
- Export at 2x (1800 x 2400) if you want it to stay sharp on a hidpi screen.
- Keep proof of licence for anything you didn't draw yourself — this ships in a
  paid product.
