# D3DX math in the game logic

## Finding

`d3dx9_29.dll` is the Direct3D helper library: textures, effects, and math (matrices,
vectors). It looks like a rendering library, so one would expect differences in it to
affect only what is drawn. That is not the case for C&C3.

The game imports 8 math functions from it. We traced every call site through the call
graph up to the virtual methods of known classes (RTTI names). Tool: `tools/xref_rtti.py`.

| Function | Used from | Part of the simulation? |
|---|---|---|
| `D3DXVec4Transform` | `BezierSegment` → **`BezierProjectileBehavior`**, `ScatterProjectileNugget`, **`PhysicsBehavior`**, `TransportContain`/`HordeContain`, `BridgeBuilderBehavior`, `SquishCollide`, `ScriptActions` | **yes** |
| `D3DXVec3CatmullRom` | position cache of game objects, read by `GameClient`, `W3DView`, draw modules; one path from `StealthUpdate` | probably not (see below) |
| `D3DXMatrixRotationX/Y/Z/Axis` | `FXParticleSystem` draw modules | no |
| `D3DXMatrixInverse`, `D3DXMatrixTranspose` | cameras, meshes, shaders (`CameraClass`, `MeshClass`, `HLodClass`, `FXShaderParameter…`), lasers, UI | no |

`D3DXVec4Transform` is called by the engine's Bézier curve helper (`BezierSegment` in the
released Generals source, which shares this code). It multiplies the four control-point
coordinates by the cubic Bézier basis matrix stored in the executable (`.data:0xb9d230`, `{-1 3 -3 1 / 3 -6 3 0 / -3 3 0 0 / 1 0 0 0}`)
and steps along the curve. The callers are logic modules: projectiles flying on curved
trajectories (artillery, missiles), physics, units leaving transports, bridges, scripts.
Where such a projectile is on a given frame decides when and where it hits, so a
different result here changes the game state.

`D3DXVec3CatmullRom` is called from a position cache of game objects: it interpolates
between the last four positions with a parameter read from a global, and the result is
cached per frame. Most callers are client code (`GameClient`, `W3DView`, draw modules). One
path comes from `StealthUpdate`, a logic module, and we have not traced what it does with
the value. The parameter looks like the time fraction between logic frames. That depends
on real time, so if the simulation used it, the game would desync even between identical
PCs. We treat it as render-only, but this one is not proven.

## Test

`tools/d3dxtest.c` calls the same functions on the same 200 000 pseudo-random inputs, with
the game's FPU mode, and writes every result to a file. The `bezier` case reproduces the
game's call exactly: the same basis matrix, control points in map coordinates
(0…5000). It was run with Microsoft's DLL (`9.11.519.0000`, from a bottle where DirectX was
installed) and with Wine's built-in DLL, on both machines.

| Configuration | bezier | vec4transform | rotation | inverse | catmullrom |
|---|---|---|---|---|---|
| Apple Silicon, Microsoft DLL | `20882bce…` | `2f287994…` | `431e710a…` | `522545e6…` | `b9e607e5…` |
| Intel, Microsoft DLL | `20882bce…` | `2f287994…` | `431e710a…` | `522545e6…` | `b9e607e5…` |
| Apple Silicon, Wine built-in | `862b6f64…` | `dd0be825…` | `82bf5b66…` | `30c8d297…` | `d0ab3251…` |
| Intel, Wine built-in | `862b6f64…` | `dd0be825…` | `82bf5b66…` | `30c8d297…` | `d0ab3251…` |

(hash of all results)

* **The CPU does not matter.** The same DLL gives bit-identical results on Rosetta 2 and
  on Intel.
* **The DLL matters.** Microsoft's and Wine's implementations disagree:

| | results differing | size of the difference |
|---|---|---|
| bezier (game's trajectory step) | 7.7% | 62% of those by 1 ulp, 19% by 2; up to 1.2% relative where terms cancel |
| vec4transform (random matrices) | 33% | |
| rotation matrices | 7% | |
| inverse | 42% | |
| catmullrom | 65% | |

So if one player's bottle has Microsoft's `d3dx9_29.dll` and the other has Wine's,
curved projectiles fly slightly differently on the two machines, and that can cause a
desync on its own, independent of Rosetta. In our own setup this was the case: the
Apple Silicon bottle had the DirectX runtime installed, the Intel one did not.

## What to do

Use the same `d3dx9_29.dll` on every machine. Wine's built-in one is the easy choice:
every bottle has it, and its results do not depend on the CPU. See
[environment.md](environment.md#d3dx9_29dll) for how to check and switch.
