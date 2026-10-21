# C&C3 desync fix: Apple Silicon ↔ Intel (CrossOver / Wine)

Command & Conquer 3: Tiberium Wars and Kane's Wrath desync in LAN/online games when one
player runs the game on an Apple Silicon Mac (x86 code translated by Rosetta 2) and the
other on an Intel machine, even with the same CrossOver version, the same bottle setup and
identical game files. Typically the game runs fine for 10–15 minutes and then reports a
desync (out of sync / "mismatch").

This repository contains:

* **a patch for the game executable** that makes its floating-point math produce
  bit-identical results on Rosetta 2 and on real Intel CPUs;
* **an environment checklist**: one more desync source we found is a different
  `d3dx9_29.dll` on the two machines (Microsoft's vs Wine's built-in), which also changes
  game-logic results;
* the analysis, measurements and test programs behind both.

## Why it desyncs (short version)

C&C3 is a lockstep RTS: players exchange only commands, and every machine simulates the
whole game world itself. That only works while every computation is bit-identical everywhere.

* The game computes angles and rotations with x87 instructions `fsin`, `fcos`, `fsincos`,
  `fptan`, `fpatan` and `fyl2x`. Rosetta 2 emulates them almost, but not exactly, like Intel
  hardware: 5–18% of results differ in the last bit of the 64-bit mantissa.
* In places the game keeps that 80-bit result on the FPU stack and subtracts from it
  (for example, the angle to a target minus the unit's current orientation). When the two
  values are close, the last-bit difference survives the rounding to `float` and ends up in
  the game state, roughly once per 75 000 such computations.
* The changed value is stored in an object (orientation, position, velocity), feeds the
  next simulation frames, and eventually flips a discrete decision: in range or not, which
  path, who fires first. From there the two worlds diverge and the game reports a desync.

After rounding to `float`, the Rosetta and Intel results of those instructions are
identical. We checked every float input in the ranges the game uses, hundreds of millions
of inputs per instruction. The patch makes the game round every such result to `float`
right away, so the last-bit difference never gets in.

Details: [docs/root-cause.md](docs/root-cause.md).

## Quick start

All players need the **same patched executable**. A patched game cannot play with an
unpatched one.

1. Quit the game on every machine.
2. Get the patcher: download the zip from
   [Releases](../../releases) or clone this repository. It needs only Python 3, which ships
   with macOS (`python3`; if macOS offers to install developer tools, accept).
3. Run it against the game folder, on **every** machine:

   ```bash
   python3 patcher/cnc3_detfp.py "$HOME/Library/Application Support/CrossOver/Bottles/<bottle>/drive_c/<path to the game>"
   ```

   It finds `cnc3game.dat` (Tiberium Wars) and `cnc3ep1.dat` (Kane's Wrath), saves the
   originals next to them as `*.orig` and patches them. Running it again does nothing.
   `--status` shows what is installed, `--restore` puts the originals back.
4. Make sure `d3dx9_29.dll` is the same on all machines, see
   [Environment checklist](#environment-checklist).
5. Play.

Known builds are patched from the bundled diffs in `patcher/patches/`. For an unknown build
the patcher falls back to the generator `patcher/detfp_gen.py`, which needs
`python3 -m pip install capstone pefile`. Then compare the resulting SHA-1 across machines:
the generator is deterministic, so the same input gives the same output.

| Game | File | Original SHA-1 | Patched SHA-1 |
|---|---|---|---|
| Tiberium Wars 1.9 | `RetailExe/1.9/cnc3game.dat` | `45b3c401accc2e3e55e49d5faafb397bc27e0295` | `492dc1c969df405eef5fc88c1de714be299873dd` |
| Kane's Wrath 1.2 | `RetailExe/1.2/cnc3ep1.dat` | `50da78b6db468dba73a1324a1cc6ffb6b5ab9a28` | `e193887cc4ee40239ad160b54bd6001608cd65fb` |

## Environment checklist

The patch only covers code inside the game executable. Everything else the simulation
touches must also be identical on all machines:

* **`d3dx9_29.dll`: important.** The game uses D3DX math in its simulation, not only for
  rendering: `D3DXVec4Transform` evaluates projectile Bézier trajectories
  (`BezierProjectileBehavior`) and is also used by physics, transport, bridge and script
  code. Microsoft's DLL and Wine's built-in one give different results in 7.7% of
  trajectory computations, on any CPU. A bottle where the DirectX runtime was installed has
  Microsoft's DLL. Use the same one everywhere, preferably Wine's built-in.
  [docs/d3dx9.md](docs/d3dx9.md)
* **`msvcr80.dll` (VC++ 2005 CRT)**: the game takes `sin`, `cos`, `atan2`, `pow`… from it.
  Wine's built-in CRT gives identical results on ARM and Intel. Microsoft's one implements
  these functions with the same x87 instructions as the game, so it would not be covered by
  the patch. Keep it built-in.
* **Same game version and data** on all machines (exe, `.big` files, patches, mods).

How to check all of this in a bottle: [docs/environment.md](docs/environment.md).

## Status

* Verified: the math tests, run on an M-series Mac (Rosetta 2, CrossOver 26.3) and on an
  Intel Mac (Coffee Lake, CrossOver 26.3); the patched game starts and runs.
* Pending: confirmation in long LAN games. Reports are welcome in Issues.

## Documentation

* [docs/root-cause.md](docs/root-cause.md): lockstep, x87 on Rosetta 2, how one bit grows into a desync, measurements.
* [docs/patch.md](docs/patch.md): what exactly the patch changes and how the patcher works.
* [docs/d3dx9.md](docs/d3dx9.md): D3DX math in the game logic, native vs built-in test.
* [docs/environment.md](docs/environment.md): what must match between machines and how to check it.
* [docs/investigation.md](docs/investigation.md): hypotheses we checked, including the ones that were wrong.
* [docs/display.md](docs/display.md): unrelated but common under CrossOver: the game window comes up stretched or shifted.
* [tools/](tools/): test programs used for the measurements.

## Notes

* Patched and unpatched builds compute differently, so they cannot play together, and
  replays recorded with one may not play back correctly on the other.
* The patch adds a few instructions per trigonometric call; the cost is negligible.
* Windows on ARM (Prism) and other x86 emulators may have the same issue; we have not
  tested them.
* This repository contains no game files. You need your own copy of the game.

## License

[MIT](LICENSE)
