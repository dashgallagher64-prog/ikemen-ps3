# Ikemen PS3 (proof of concept)

A from-scratch C port skeleton for PS3 homebrew (PSL1GHT), meant to grow into a
MUGEN/Ikemen-GO-compatible engine. **It is not Ikemen GO itself** (that is Go and
cannot be compiled for the PS3). Ikemen GO's MIT-licensed source is the behaviour
reference; the sprite decoders here were checked against its `src/image.go`.

## Build the .pkg (easy way)

    ./build.sh

Needs only Docker. The image downloads the project's **pre-built** compiler + PSL1GHT from
[ps3dev/ps3dev releases](https://github.com/ps3dev/ps3dev/releases) (~170 MB), so nothing is
compiled and the first run takes a few minutes.
Output: `ikemen-ps3.pkg` (and `ikemen-ps3.gnpdrm.pkg`).

**No Docker?** Push this folder to a GitHub repo. `.github/workflows/build.yml` downloads the same
pre-built toolchain, builds the package on every push and attaches it as the `ikemen-ps3-pkg`
artifact (Actions tab). It also runs the host tests.

**Own toolchain?** Install the ps3dev release (or build it from
[ps3dev/ps3dev](https://github.com/ps3dev/ps3dev)), export `PS3DEV` / `PSL1GHT` and add
`$PS3DEV/bin`, `$PS3DEV/ppu/bin` to `PATH`, then `make pkg`. (`tools/fetch-ps3dev.sh` downloads
the release for you.)

Note: `ps3toolchain` alone only builds the compiler. PSL1GHT now comes from `ps3dev/ps3dev`, so
don't build with the old `ps3dev-docker` image; it lacks PSL1GHT.

## Install & add content

Install the `.pkg` from the XMB (Game > Install Package Files). Content lives in:

    /dev_hdd0/game/IKEMEN001/USRDIR/
        chars/<name>/<name>.def (+ sff, air, cmd, cns, snd ...)
        stages/<name>.def (+ sff)
        data/system.def, data/<Pack>/system.def

Copy folders over FTP (multiMAN/webMAN) and they show up on next launch. Nothing is
hardcoded, so adding a character, stage or screenpack never needs a rebuild.
`USRDIR/ikemen.log` lists what was found; check it first if something doesn't show up.

Controls: D-pad up/down move, X confirm, O back, L1/R1 cycle screenpack, PS button quit.

## Status

| Part | State |
|------|-------|
| DEF/INI parser | done, host-tested |
| Content scanner (chars / stages / screenpacks) | done, host-tested |
| SFF v1: PCX decode, shared palettes, linked sprites | **done, verified on real files** |
| SFF v2: raw 8-bit, RLE8, RLE5, LZ5, palettes, linked sprites | **done, verified on real files** (RLE5 not seen in the sample set) |
| Single-sprite loading (`sff_load_ex`) for fast select-screen previews | done, tested |
| SFF v2 PNG sprites (formats 10/11/12) and raw 24/32-bit | **done, verified on real files** (kfm.sff's 120x140 PNG portrait decodes pixel-perfectly via our own inflate) |
| Menu: char -> stage -> ready, with portrait (9000,0) and standing sprite (0,0) | done; logic run on the host with a fake pad |
| RSX framebuffer, pad, sysutil exit | written against PSL1GHT headers, **never run on a PS3** |
| Makefile / pkg build | modeled on the PSL1GHT samples; **never run with the real toolchain** |
| AIR, CMD, CNS state-controller VM, collision, fight loop, Lua motifs, audio | not started |

Verification honesty: the portable code (DEF, SFF, content, sprite decode, menu logic) runs
and is tested on a normal computer under ASan/UBSan. The PS3-only files (`gfx.c`, `main.c`
platform calls, `Makefile`) were checked by reading the real PSL1GHT headers/samples and by
syntax-checking, but nobody has compiled or run them on the real toolchain or console yet.
Expect to fix small things on first build, and tell me what breaks.

## Tests

    sh tests/run.sh                          # DEF / SFF probe / content scanner on a fake tree
    sh tests/run_png.sh                       # PNG round-trip: encode with Python's zlib, decode with us, byte-compare
    sh tests/run_real.sh <screenpack repo>   # decode real SFFs (needs a checkout of
                                             #   github.com/ikemen-engine/Ikemen_GO-Elecbyte-Screenpack)
    sh tests/run_ui.sh <content root> <out> [keys]   # run the real menu on the host, dump screenshots

Sprite-format reference tools: `tests/sff_dump.c` (stats + contact sheet), `tests/sff_filter_test.c`.

## Roadmap

1. ~~SFF v2 PNG sprites (small inflate + PNG unfilter; formats 10/11/12)~~ - done
2. AIR parser + animation player
3. CMD/CNS/ST parsing and the trigger/state-controller VM (the big part)
4. Collision, hitdefs, projectiles, helpers
5. Embedded Lua 5.x for Ikemen-style motifs (`system.def`, `fight.def`, select/menu)
6. Audio: SND + WAV via libaudio
7. Move rendering to RSX sprite batches if CPU blitting is too slow on the Cell

## Legal

Bring your own content. Don't redistribute characters/stages you don't have rights to.
The default icon is the stock PSL1GHT `ICON0.PNG`; replace `ICON0.PNG` with your own.
