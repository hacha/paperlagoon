# PaperLagoon (working title)

A generative environmental-music firmware for the M-VAVE FM-1, after 1980s Japanese *kankyō ongaku*
(and, at most, Susumu Yokota's hazy pulse). It plays by itself; you steer it from outside.

Status: just started from fm1-base's template.

## Build

```sh
git submodule update --init --recursive
./build.sh --host                     # host simulator (a C compiler is enough)
app/build/host/paperlagoon_host app/tests/basic.txt app/build/host
./build.sh --web                      # browser emulator (Emscripten)
./build.sh                            # device package (JieLi toolchain + AC79 SDK)
```

## Licence

GPL-3.0-only (see LICENSE). Built on [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0) and
felucca-bsp. Files taken from fm1-base's template keep their MPL-2.0 notices.
