# Investigation notes

How we got to the cause, including the ideas that turned out wrong. Setup: an Apple
Silicon MacBook (CrossOver 26.3, x86 code through Rosetta 2) and an Intel MacBook
(Coffee Lake, CrossOver 26.3). Same CrossOver version and same game files on both. The
desync appeared reliably after 10–15 minutes of a LAN game.

## 1. Is the math different on the two CPUs? Yes, in x87 transcendentals

`tools/fputest.c` runs every x87 and a set of SSE operations on 2^20 inputs each and hashes
both the 80-bit and the float-rounded results. Arithmetic, `fsqrt`, `frndint`, conversions,
NaN propagation, denormals and the SSE approximations `rcpss`/`rsqrtss` are identical. The
80-bit results of `fsin`, `fcos`, `fsincos`, `fptan`, `fpatan`, `fyl2x`, `f2xm1` differ
in 5–18% of cases, always by 1 ulp of the 64-bit mantissa (rarely 2). Table:
[root-cause.md](root-cause.md).

`tools/exhaust.c` and `tools/fltdump.c` then ran every float input in the ranges the game
uses and compared the float-rounded results: identical. The only exception is `f2xm1` with
|x| > 1, outside its defined range. (A first exhaustive run seemed to show differences for
`atan(1/x)` and `log2`. Running the same binary again on both machines gave identical
results. The first comparison had mixed outputs from two builds of the test.)

`tools/relangle.c` reproduces the game's pattern `atan2(dy, dx) - orientation` without
intermediate rounding. About 1 in 75 000 results differs after rounding to float. That is
how the difference gets into the game state.

## 2. Is the FPU mode set differently or reset on ARM? No

Hypothesis: the game sets x87 precision to 24 bits in `setFPMode()`, and something (the
pathfinder, a DLL, Rosetta's handling of `fldcw`) resets it on one machine, so the two
machines compute at different precision.

We added a probe to the game executable (`tools/fpprobe/`): a hook at the entry of
`setFPMode()` that records the caller, the x87 control word and MXCSR on every call into a
table in a new section, and a reader that pulls the table out of the running process. We
ran the game on both machines (about 5 900 calls on Intel, 62 000 on Apple Silicon):

* the control word before every call is `0x007F` (PC24, round-nearest, all masked) on
  both, except the very first call at startup, which sees the default `0x027F`;
* MXCSR has the same masks and rounding on both. Only the sticky exception flags vary,
  as expected;
* the same four callers, in the same proportions (3 : 2 : 1 for the three frequent ones).

So nothing resets the mode differently on ARM. `_set_SSE2_enable(0)` is also called on
both, making the CRT use its x87 code paths.

## 3. Does the CRT differ? No, as long as it is Wine's

The executable's manifest requests `Microsoft.VC80.CRT`. `tools/crtprobe.c` loads
`msvcr80.dll` and `d3dx9_29.dll` the same way, through the same manifest, and hashes the
results of every math function the game imports, plus `strtod`/`atof`, the order of
`qsort` with equal keys, and `rand`. Both machines load Wine's built-in `msvcr80.dll`, and
all results are identical.

## 4. Does d3dx9 matter? Yes, if the DLLs differ

The same probe showed the Apple Silicon bottle loading Microsoft's `d3dx9_29.dll` and the
Intel bottle Wine's, with different results. Tracing the call sites showed that
`D3DXVec4Transform` is used for projectile trajectories, which is game logic. A dedicated
test (`tools/d3dxtest.c`) on both machines with both DLLs showed that the DLL, not the
CPU, decides the result. Details: [d3dx9.md](d3dx9.md).

## 5. Kane's Wrath

KW 1.2 has the same engine code with more trig sites (242 vs 220) and the same issue.
In our experience KW desynced sooner than TW. More sites would fit that, but we have no
measurements for it.

## Approaches we did not take

* **Capturing the state checksums and finding the first diverging frame.** With a desync
  after 10–15 minutes and many causes possible, this would take long, and the math tests
  already showed a concrete mechanism.
* **Forcing 80-bit or SSE math.** It does not remove the transcendental difference.
* **Replacing `fsin` and friends with a software implementation.** Rounding to float is
  much smaller and keeps the results as close as possible to the original game.
