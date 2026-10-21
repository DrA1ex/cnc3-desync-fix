# Test programs

Small Windows programs used for the measurements in [docs/](../docs/). They are 32-bit,
like the game, and are run inside a CrossOver bottle on each machine; the outputs are then
compared on the Mac.

Build with mingw-w64 (`brew install mingw-w64`):

```bash
i686-w64-mingw32-gcc -O1 -static -o fputest.exe fputest.c
i686-w64-mingw32-gcc -O1 -static -o exhaust.exe exhaust.c
i686-w64-mingw32-gcc -O1 -static -o fltdump.exe fltdump.c
i686-w64-mingw32-gcc -O1 -msse2 -mfpmath=sse -static -o relangle.exe relangle.c
i686-w64-mingw32-windres crtprobe.rc -O coff -o crtprobe.res
i686-w64-mingw32-gcc -O1 -msse2 -mfpmath=sse -static -o crtprobe.exe crtprobe.c crtprobe.res
i686-w64-mingw32-gcc -O1 -static -o d3dxtest.exe d3dxtest.c
```

Run, for example:

```bash
cp fputest.exe ~/Library/Application\ Support/CrossOver/Bottles/<bottle>/drive_c/
/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine --bottle <bottle> 'C:\fputest.exe'
```

| Program | What it does |
|---|---|
| `fputest.c` | x87 (PC24/53/64) and SSE operations on 2^20 inputs each; prints hashes of 80-bit and float results; `fputest.exe [N] [raw.bin]` also writes the raw results for an ulp comparison |
| `exhaust.c` | every float input in a range through one transcendental, float-rounded; one hash per 2^20 inputs. `exhaust.exe sin` runs one function |
| `fltdump.c` | raw float results for one block of 2^20 inputs, for finding the exact inputs that differ: `fltdump.exe 3f800000 out.bin` |
| `relangle.c` | the game's `atan2(dy, dx) - orientation` pattern kept in 80-bit, then rounded; counts differing floats |
| `crtprobe.c` | loads `msvcr80.dll` through the game's manifest and `d3dx9_29.dll`, reports built-in or Microsoft's, hashes all math the game imports |
| `d3dxtest.c` | D3DX math the game imports, including its Bézier trajectory step, with the game's FPU mode; `d3dxtest.exe d3dx9_29.dll out.bin`, or a path to a specific DLL |
| `xref_rtti.py` | static call-site tracer: from a call site up the call graph to virtual methods, printing class names from RTTI (`pip install pefile`) |
| `fpprobe/` | hook on the game's `setFPMode()` that records the caller, x87 control word and MXCSR of every call, and a reader that pulls the table from the running game |
| `display/` | display mode, monitor and window geometry as the game sees them ([docs/display.md](../docs/display.md)) |

To find the inputs that differ, run a program on both machines, copy the output files to
one Mac and compare them with a few lines of numpy.

## fpprobe

```bash
cd tools/fpprobe
python3 add_probe.py cnc3game.dat cnc3probe.dat 809274 80927a   # TW 1.9: setFPMode entry and return point
```

`add_probe.py` assembles `probe.s` (needs `i686-w64-mingw32-as` and `objcopy`), adds an
RWX section with the code and a table, and replaces the first instruction of `setFPMode`
with a jump to the probe. It prints the table address. Run the game from `cnc3probe.dat`
in place of `cnc3game.dat`, then:

```bash
i686-w64-mingw32-gcc -O1 -static -o fpreader.exe reader.c
wine --bottle <bottle> 'C:\fpreader.exe' <table address>
```

The addresses above are for TW 1.9. For another build, find `setFPMode` (a function that
calls `_fpreset` and then `_controlfp(0x20000, 0x30300)`).
