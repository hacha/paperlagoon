# PaperLagoon (working title)

A generative environmental-music firmware for the M-VAVE FM-1, after 1980s Japanese *kankyō ongaku*
(and, at most, Susumu Yokota's hazy pulse). It plays by itself; you steer it from outside.

Status: a first sketch. FM voices through AMB-1's tape echo and plate, played by a placeholder generator
(a motif on a pentatonic scale, with long rests). Tested in the host simulator; runs on an FM-1 (installed with base/tools/fm1_install.py, 2026-11-11).

## Controls

| Control | Does |
|---|---|
| SELECT | turns through the pages: 1 the lagoon, 2..4 the kit's voices (the header shows n/4) |
| HOME | the lagoon (lit there) |
| EDIT | the voices (lit there); again: the next voice; held: the voice as the kit has it |
| PRESETS | the kit: six sets of three voices (lead, answer, low); edits are kept per kit |
| K1..K4 | the lagoon: Density, Bright, Space, Echo. A voice: Color (the FM ratio), Bright, Decay, Soft (the attack) |
| PLAY | plays or rests |
| SAVE | keeps all of it (lit when something is not saved) |

Kits: PAPER LAGOON, WOODEN RAIN, GLASS ORCHARD, SLOW BREATH, IRON SNOW, HAZE ARCHIVE.
The keys only light, for now.

## Build

```sh
git submodule update --init --recursive
./build.sh --host                     # host simulator (a C compiler is enough)
app/build/host/paperlagoon_host app/tests/basic.txt app/build/host   # and app/tests/tones.txt
./build.sh --web                      # browser emulator (Emscripten)
./serve.sh [port]                     # serve it on http://localhost:8000/
./build.sh                            # device package (JieLi toolchain + AC79 SDK)
```

## Licence

GPL-3.0-only (see LICENSE). Built on [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0) and
felucca-bsp. The plate and the echo come from AMB-1 (MPL-2.0); see LICENSING.md.
