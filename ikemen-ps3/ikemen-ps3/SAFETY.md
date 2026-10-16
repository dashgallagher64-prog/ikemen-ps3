# Safety: can this game brick a PS3?

**Short version:** the program has no code path that can damage the console's firmware or system
files. It is an ordinary HDD game that reads content files and writes one log file. I cannot
promise "impossible" for any software, and it has not yet been run on a real console, so this
document says exactly what is guaranteed, how that is checked, and what can still go wrong.

## What the game does to your console

| It does | It never does |
|---|---|
| Read characters, stages and screenpacks from its own folder `/dev_hdd0/game/IKEMEN001/USRDIR/` | Write, delete, rename or create anything else (no `remove`, `unlink`, `rename`, `mkdir`, ...) |
| Write **one** file: `USRDIR/ikemen.log` (overwritten at each launch) | Touch `dev_flash`, USB drives, save data, system settings or other games |
| Read the controller, draw to the screen with the same RSX setup calls the PSL1GHT samples use | Make LV2/LV1 syscalls, peek/poke memory, load modules, mount anything, run other programs |
| Exit to the XMB when you press the PS button | Use the network, install anything, or call firmware-update functions |

The package is a plain game: `CATEGORY=HG`, its own title id `IKEMEN001`, `ATTRIBUTE=0` (no special
rights). Ordinary game installs cannot overwrite system files.

## How that is enforced (not just promised)

* `tests/check_safety.sh` scans the real code (comments removed) and **fails** if it finds any
  delete/rename/mkdir/exec/mount/syscall/network call, any `fopen` that is not read-only (except
  the one log file), any write that is not to that log, a changed package category or attribute,
  or an unexpected library. I tested the gate by injecting 13 different violations; all 13 failed it.
* The GitHub workflow runs this gate first, and **the package is only built if it passes**.
* Content can't escape its folder: a `.def` that says `sprite = ..\..\something` or an absolute
  path is ignored (`tests/safe_rel_test.c`).

## Bad or malicious content

Characters you download are untrusted files. The sprite loader has hard limits so a corrupt or hostile
`.sff` cannot make the app run out of the PS3's small memory: files over 64 MB are refused, at most
65,536 sprites and 2,048 palettes, at most 2048x2048 pixels per sprite, and at most 40 MB of
decoded pixels per file. Tests:

* 10 hand-built hostile files (huge sprite counts, 2 GB of claimed pixels, palette bombs, header
  cycles, truncation, ...) all finish in well under a second and keep at most ~38 MB, even with the
  process capped at 80 MB.
* 27,500 randomly corrupted copies of real characters, stages and fonts (7,500 under
  AddressSanitizer/UBSan, 20,000 under a 150 MB memory cap), run after the limits were added:
  no crash, no hang, no memory error. `run_safety.sh` repeats a 9,000-file version of this.

`tests/run_safety.sh [screenpack checkout]` reruns all of it.

## What can still go wrong (honest list)

1. **Crash or freeze.** A bug of mine, or content that is broken in a way the tests didn't think of, can
   crash or freeze the game. That returns you to the XMB or needs a power-off/on. It does not damage
   the system, and it is not a brick.
2. **Not run on real hardware by me.** The package builds, and your console recognized the character
   you added, but the only complete test is a run on the console. Everything that does not need a PS3
   (parsing, decoding, menu logic) is tested on a PC; the PS3-specific screen/controller code follows the
   PSL1GHT samples but is the part I could not exercise.
3. **Memory on the console.** The limits above were tested with address-space caps on a PC, not on the PS3.
   Very large HD characters (SFF files over 64 MB) are refused and will show "NO PREVIEW".
4. **Things outside this program.** Real PS3 bricks come from flashing firmware, installing custom firmware
   incorrectly, downgrading, or using low-level system tools. Installing this package does none of those.
   Only install a `.pkg` that you built from your own copy of this repo.
5. **Disk full / read-only disk.** The log write simply fails; the game carries on.

## Undoing everything

Delete "Ikemen PS3" from the XMB game list. Its folder and the one log file go with it.
