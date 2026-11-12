# Licensing

PaperLagoon as a whole is distributed under **GPL-3.0-only** (LICENSE). Its parts keep their own notices:

| Part | From | Licence |
| --- | --- | --- |
| `app/app.c`, `app/app.h`, `app/dsp/lagoon.c`, `app/dsp/lagoon.h`, `app/dsp/age.c`, `app/dsp/pulse.c`, `app/words.tsv`, `app/tests/`, `serve.sh`, `install.sh`, `test.sh` | PaperLagoon, by hacha | GPL-3.0-only |
| `app/docs/` (the manuals and their screenshots), `app/site/` (the front page, the title image and `title.py`, which draws it), `app/fm1.toml`, `README.md` | PaperLagoon, by hacha | GPL-3.0-only |
| `app/dsp/amb_parts.c` (the command queue, random numbers, sine table, slow modulators, two-operator FM voice, output mix) | [AMB-1](https://github.com/charlesvestal/fm1-amb) by Charles Vestal, with changes | MPL-2.0 |
| `app/dsp/plate.c` (Dattorro's plate) | [AMB-1](https://github.com/charlesvestal/fm1-amb) by Charles Vestal, its plate after FoMni | MPL-2.0 |
| `app/dsp/echo.c` (tape echo and Echoplex) | [AMB-1](https://github.com/charlesvestal/fm1-amb) by Charles Vestal, with changes | MPL-2.0 |
| `build.sh` | [AMB-1](https://github.com/charlesvestal/fm1-amb)'s by Charles Vestal, its name changed | MPL-2.0 |
| `.github/workflows/release.yml` | [fm1-base](https://github.com/charlesvestal/fm1-base)'s template by Charles Vestal (after X0X's and Melodee's), filled in | MPL-2.0 |
| `base/` (submodule) | [fm1-base](https://github.com/charlesvestal/fm1-base) by Charles Vestal, on felucca-bsp from [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita | MPL-2.0 |

`app/app.c` started from fm1-base's template app (MPL-2.0) and was rewritten; a few of its idioms remain. fm1-base
says of its template: changes to its files are published under MPL-2.0, your own files may use any licence.

MPL-2.0 is file-level: the MPL files stay MPL-2.0 (changes to them too) and may be combined with GPL code
(MPL-2.0 section 3.3).

Note: AMB-1's LICENSING.md gives its plate (from FoMni-1) as MPL-2.0; DUB-1's gives the same FoMni plate as
GPL-3.0-only. Either way PaperLagoon is GPL-3.0-only, so this does not change what may be done with it.

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Fonts: Barlow Semi Condensed (The Barlow Project Authors), Terminus (Dimitar Toshkov Zhekov), drawn on the screen and built into the firmware | SIL OFL 1.1 | `base/assets/fonts/`, with their licences (`Barlow-OFL.txt`, `Terminus-LICENSE.txt`) |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"M-VAVE" and "FM-1" are trademarks of their respective owners, used here only to say what hardware PaperLagoon runs
on. "Felucca" and "Hügelton Instruments" are names of Hügelton Instruments. PaperLagoon is independent firmware; it
is not affiliated with, endorsed by or supported by any of them.

## Radio

PaperLagoon never enables the Bluetooth / Wi-Fi radio of the hardware.
