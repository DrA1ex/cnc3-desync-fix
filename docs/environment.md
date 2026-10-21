# What must match between machines

The patch makes the game executable itself deterministic. Everything the simulation calls
outside of it must be identical on all machines too. "Same CrossOver version and same game
files" is not enough: bottles differ in which DLLs are Wine's built-in ones and which are
Microsoft's, depending on what was installed into them.

All paths below are inside a bottle:
`~/Library/Application Support/CrossOver/Bottles/<bottle>/drive_c`.

## Checklist

| What | Why | Recommended |
|---|---|---|
| Game executable | the patch | the same patched build, compare SHA-1 |
| Game data (`.big`, patches, mods, language) | the simulation reads unit and weapon data from it | identical |
| `d3dx9_29.dll` | `D3DXVec4Transform` computes projectile trajectories, see [d3dx9.md](d3dx9.md) | Wine built-in on all machines |
| `msvcr80.dll` (VC++ 2005 CRT) | the game takes `_CIsin`, `_CIcos`, `_CItan`, `_CIatan`, `_CIatan2`, `_CIasin`, `_CIacos`, `_CIexp`, `_CIlog`, `_CIlog10`, `_CIpow`, `_CIfmod`, `_CIsqrt`, `floor`, `ceil`, `ldexp`, `frexp`, `strtod`, `atof`, `rand`, `qsort` from it | Wine built-in on all machines |
| Renderer (D3DMetal, DXVK, wined3d) | rendering only | anything |

## d3dx9_29.dll

A bottle has Microsoft's `d3dx9_*.dll` when the DirectX runtime (June 2010 redistributable,
"DirectX for Modern Games" and similar) was installed into it, sometimes as a dependency of
another installer. Otherwise it has Wine's built-in DLL.

Check which one a bottle has:

```bash
cd ~/Library/Application\ Support/CrossOver/Bottles/<bottle>/drive_c/windows/syswow64
dd if=d3dx9_29.dll bs=1 skip=64 count=16 2>/dev/null; echo
```

`Wine builtin DLL` means built-in. Anything else is Microsoft's DLL, and then
`shasum d3dx9_29.dll` should match on all machines.

The DLL that is actually loaded also depends on the bottle's DLL overrides. To make the
game use the built-in one regardless of which file is there, add an override, either in
the bottle's *Wine Configuration* control panel (tab *Libraries*: add `d3dx9_29`, *Edit* →
*Builtin (Wine)*) or from Terminal:

```bash
/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine --bottle <bottle> \
    reg add 'HKCU\Software\Wine\DllOverrides' /v d3dx9_29 /d builtin /f
```

We checked that with this override a process gets the built-in DLL even though Microsoft's
file is in the bottle. `tools/d3dxtest.c` and `tools/crtprobe.c` print which DLL was loaded.

## msvcr80.dll

The game requests the VC++ 2005 CRT through its manifest (`Microsoft.VC80.CRT`). In the
bottles we tested, Wine's built-in `msvcr80.dll` was loaded on both machines, and its math
gave bit-identical results on Apple Silicon and Intel (`tools/crtprobe.c`).

If the Microsoft Visual C++ 2005 Redistributable was installed into a bottle, the
game may get Microsoft's CRT instead. Its `_CIsin`, `_CIatan2`, `_CIexp`… use the x87
instructions that differ on Rosetta 2, and the patch does not cover them. We did not test
that combination; avoid it. `tools/crtprobe.c` prints which `msvcr80.dll` is loaded,
through the same manifest as the game.

## Game files

```bash
cd ~/Library/Application\ Support/CrossOver/Bottles/<bottle>/drive_c/<game>
find . -type f \( -iname '*.big' -o -iname '*.dat' -o -iname '*.skudef' -o -iname '*.exe' \) -print0 | sort -z | xargs -0 shasum > ~/cnc3-files.txt
```

Run on each machine and compare the lists with `diff`.

## Things that do not matter

* The x87/SSE control words: the game sets them itself, and we measured them in the running
  game. They are identical on both architectures ([investigation.md](investigation.md)).
* CPU model, GPU, resolution, renderer and graphics settings.
* macOS version, as long as the CrossOver version is the same. We have not seen CrossOver
  versions matter, but we tested only 26.3.
