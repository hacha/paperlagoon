# Licensing

PaperLagoon as a whole is distributed under **GPL-3.0-only** (LICENSE). Its parts keep their own notices:

| Part | From | Licence |
| --- | --- | --- |
| `app/app.c`, `app/app.h`, `app/dsp/lagoon.c`, `app/dsp/lagoon.h`, `app/tests/`, `build.sh`, `serve.sh`, `install.sh`, `test.sh` | PaperLagoon | GPL-3.0-only |
| `app/dsp/amb_parts.c` (the command queue, random numbers, sine table, slow modulators, two-operator FM voice, output mix) | [AMB-1](https://github.com/charlesvestal/fm1-amb) by Charles Vestal, with changes | MPL-2.0 |
| `app/dsp/plate.c` (Dattorro's plate) | [AMB-1](https://github.com/charlesvestal/fm1-amb) by Charles Vestal, its plate after FoMni | MPL-2.0 |
| `app/dsp/echo.c` (tape echo and Echoplex) | [AMB-1](https://github.com/charlesvestal/fm1-amb) by Charles Vestal | MPL-2.0 |
| `base/` (submodule) | [fm1-base](https://github.com/charlesvestal/fm1-base) by Charles Vestal, on felucca-bsp from [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita | MPL-2.0 |

MPL-2.0 is file-level: the MPL files stay MPL-2.0 (changes to them too) and may be combined with GPL code
(MPL-2.0 section 3.3).

Note: AMB-1's LICENSING.md gives its plate (from FoMni-1) as MPL-2.0; DUB-1's gives the same FoMni plate as
GPL-3.0-only. Either way PaperLagoon is GPL-3.0-only, so this does not change what may be done with it.
