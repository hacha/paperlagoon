# PaperLagoon (working title)

A generative environmental-music firmware for the M-VAVE FM-1, after 1980s Japanese *kankyō ongaku*
(and, at most, Susumu Yokota's hazy pulse). It plays by itself; you steer it from outside.

Status: a first sketch. FM voices through AMB-1's tape echo and plate, played by a placeholder generator
(a motif on a pentatonic scale, with long rests). Tested in the host simulator; runs on an FM-1 (installed with base/tools/fm1_install.py, 2026-11-11).

## Build

```sh
git submodule update --init --recursive
./build.sh --host                     # host simulator (a C compiler is enough)
app/build/host/paperlagoon_host app/tests/basic.txt app/build/host
./build.sh --web                      # browser emulator (Emscripten)
./serve.sh [port]                     # serve it on http://localhost:8000/
./build.sh                            # device package (JieLi toolchain + AC79 SDK)
```

## Licence

GPL-3.0-only (see LICENSE). Built on [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0) and
felucca-bsp. The plate and the echo come from AMB-1 (MPL-2.0); see LICENSING.md.
