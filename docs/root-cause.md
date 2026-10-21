# Root cause: x87 transcendentals on Rosetta 2

## Lockstep

C&C3 runs on the SAGE engine, which uses deterministic lockstep networking. Players send
each other only their commands ("these units move there", "build this"). Every machine
runs the full simulation for every player, and the machines periodically exchange a
checksum of the game state (CRC). If the checksums differ, the game is out of sync and stops.

So the simulation must be **bit-for-bit** identical on all machines. A difference in the
last bit of one float is enough, because nothing ever corrects it: the state is never sent
over the network, only recomputed.

## The game's floating-point setup

At startup the game calls `_set_SSE2_enable(0)`, so the CRT math functions use x87 code.
Before simulation frames it calls its own `setFPMode()`, which does `_fpreset()` followed by
`_controlfp(_PC_24 | _RC_NEAR, _MCW_PC | _MCW_RC)`: x87 precision 24 bits (float),
round-to-nearest, all exceptions masked, control word `0x007F`. The game code itself is a
mix of SSE scalar float arithmetic and x87. The x87 part is mostly loads and stores plus
the transcendental instructions.

We measured the control word in the running game on both machines (see
[investigation.md](investigation.md)); it is the same.

## What Rosetta 2 does differently

Rosetta 2 translates x86 code to ARM. Ordinary arithmetic (`fadd`, `fmul`, `fdiv`,
`fsqrt`, `frndint`, all of SSE) is reproduced exactly; we found no difference in hundreds of
millions of operations. The transcendental instructions are computed by Rosetta's own
routines, and the last bit of their 80-bit result sometimes differs from what Intel
hardware returns:

| Instruction | Inputs | Results differing from Intel (80-bit) | Size of the difference |
|---|---|---|---|
| `fsin` | [-3.2, 3.2] | 7.8% | 1 ulp of the 64-bit mantissa |
| `fcos` | [-3.2, 3.2] | 4.8% | 1 ulp |
| `fsincos` | [-3.2, 3.2] | 4.7% | 1 ulp |
| `fptan` | [-1.5, 1.5] | 17.8% | 1 ulp (a few cases 2 ulp) |
| `fpatan` | [-1000, 1000] | 8.9% | 1 ulp |
| `fyl2x` | [0.001, 1000] | 7.2% | 1 ulp |
| `f2xm1` | [-1, 1] | 5.1% | 1 ulp |
| `fsqrt`, `frndint`, `fadd`/`fmul`/`fdiv` chains | | 0 | |

(2^20 random inputs per row, precision control PC24 as in the game, `tools/fputest.c`.)

A 1-ulp difference in a 64-bit mantissa is about 5·10⁻²⁰ relative. When the result is
rounded to `float` (24-bit mantissa), that is lost, and the rounded results are identical:

| Instruction | Tested inputs (every float in the range) | Float results differing |
|---|---|---|
| `fsin`, `fcos`, `fsincos` (both outputs) | 2⁻²⁷ … 2⁹, ~300 M each | 0 |
| `fptan` | 2⁻²⁷ … 2¹, ~235 M | 0 |
| `fpatan` as atan(x) and atan(1/x) | 2⁻²⁷ … 2²⁴, ~430 M each | 0 |
| `fyl2x` as log2(x) | 2⁻²⁷ … 2²⁴, ~430 M | 0 |

(`tools/exhaust.c` and `tools/fltdump.c`. Negative inputs were covered by the random tests only.) The one exception
is `f2xm1` outside its defined input range |x| ≤ 1, where Intel documents the result as
undefined. The game executable does not contain `f2xm1`.

## How one bit reaches the game state

If the game always stored transcendental results straight to a `float`, there would be no
problem. It does not. A real sequence from Tiberium Wars 1.9 (`0x64518d`):

```
fld   dword [ebp+0x8]
push  ecx
fld   dword [ebp+0xc]
fpatan                    ; ST0 = atan2(y, x), 80-bit, last bit may differ
fsub  dword [ebp-0x8]     ; ST0 = atan2(y, x) - v, still 80-bit
fstp  dword [esp]         ; rounded to float only now
```

Of the 54 `fpatan` in the TW executable, 14 feed their result directly into `fadd`/`fsub`
like this, and one returns it in `ST0` to the caller. `fsin`, `fcos` and `fsincos` results
are used the same way.

When the unit almost faces the target, `angle` and `orientation` are close. Their
difference is a small number, while the 1-ulp error of `atan2` stays the same in absolute
terms. Relative to the small result it is much larger, and now the rounding to `float` can
go different ways on the two machines. In `tools/relangle.c` this pattern produces a
different float in about 1 of 75 000 evaluations (1.3·10⁻⁵).

## How it accumulates

Nothing accumulates in a register or a counter. The 80-bit value lives on the FPU stack for
only a few instructions. What persists is the `float` written to the game state:

1. A unit's turn step, a projectile's direction or a position comes out one float-ulp
   different on one machine.
2. The value is stored in the object and the next simulation frame starts from it:
   `position += velocity · dt`, `orientation += turn`. Everything computed from it differs
   slightly too.
3. Many such differences disappear: the unit gets a new order, the projectile explodes, a
   value is clamped or snapped. Some survive.
4. Eventually a surviving difference meets a discrete decision: is the target within
   range, which pathfinding cell is the unit in, which of two equal paths is shorter, does a
   shot happen on this frame or the next. One machine says yes, the other no.
5. From that point the worlds really differ (a unit dead on one machine, alive on the
   other), the next state checksum differs, and the game declares a desync.

Every single event is rare, and the game needs many units and many frames before a
surviving difference hits a decision. That is why the desync usually comes 10–15 minutes
into a game, and sooner in big battles.

## The fix

Round every transcendental result to `float` immediately after the instruction:

```
fpatan
lea  esp, [esp-4]
fstp dword [esp]          ; round ST0 to float
fld  dword [esp]
lea  esp, [esp+4]
```

`lea` is used instead of `sub`/`add` to leave the CPU flags alone. For `fsincos` and `fptan`,
which leave two values, both are rounded (`fxch` around a second rounding). After that
the value that continues through the 80-bit code is the float-rounded one, which is
identical on Rosetta and Intel, and all further x87 arithmetic is exact on both.

This changes the game's results slightly compared to the unpatched game (values that were
80-bit are now float), so **all players need the patched build**.

How the instructions are found and replaced: [patch.md](patch.md).
