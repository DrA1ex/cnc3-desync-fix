# The patch

## What changes in the executable

Every `fsin`, `fcos`, `fsincos`, `fptan`, `fpatan` and `fyl2x` instruction in the game's
`.text` section is redirected to a small stub in a new section `.detfp`. The stub executes
the original instruction and then rounds its result to `float`
(see [root-cause.md](root-cause.md#the-fix)).

| | TW 1.9 `cnc3game.dat` | KW 1.2 `cnc3ep1.dat` |
|---|---|---|
| `fsin` | 59 | 67 |
| `fcos` | 44 | 51 |
| `fsincos` | 45 | 49 |
| `fptan` | 17 | 17 |
| `fpatan` | 54 | 57 |
| `fyl2x` | 1 | 1 |
| **patched** | **220 / 220** | **242 / 242** |

The executable contains no `f2xm1`, `fscale` or `fprem`. The trigonometry inside the CRT
(`_CIsin`, `_CIatan2`…) is in `msvcr80.dll`, see [environment.md](environment.md).

Other changes to the file: one new section header, `NumberOfSections`, `SizeOfImage`, and
the PE checksum is set to 0 (it is not checked for executables). Nothing else moves, so
addresses and imports stay the same.

## How a site is redirected

An x87 instruction like `fpatan` is 2 bytes and a `jmp rel32` is 5, so the patch moves a
small window of neighbouring instructions into the stub together with it:

```
original                              patched
  fld   dword [ebp-0x24]                jmp   .detfp+0x780
  fld   dword [ebp-0x28]                nop
  fpatan                                ...
  fstp  dword [edi]

.detfp+0x780:
  fld   dword [ebp-0x24]
  fld   dword [ebp-0x28]
  fpatan
  lea   esp, [esp-4]
  fstp  dword [esp]
  fld   dword [esp]
  lea   esp, [esp+4]
  jmp   <instruction after the window>
```

A window is valid only if:

* it is at least 5 bytes and made of whole instructions;
* nothing can jump into the middle of it. The generator collects all direct branch targets
  and also every dword anywhere in the file that points into `.text` (switch tables,
  vtables, callbacks, exception handlers), which is deliberately pessimistic;
* every instruction in it can run at another address: relative `call`/`jmp`/`jcc` are
  re-encoded with 32-bit displacements, `loop`/`jecxz` are refused;
* a control transfer can only be its last instruction.

The smallest valid window is chosen, with up to 4 instructions before and 5 after the
site.

### Sites that are branch targets

Two sites per game have no valid window: the instruction itself is a branch target, and so
is the instruction after it. Example from TW, a function that returns `atan2` in `ST0`:

```
6deb41:  jmp   6deb6b            ; one path into fpatan
...
6deb66:  fld   dword [ebp+0x10]
6deb69:  fld   dword [eax]
6deb6b:  fpatan                  ; branch target
6deb6d:  pop   esi               ; branch target (three other paths return through here)
6deb6e:  leave
6deb6f:  ret   0xc
```

For these, every way into the site is redirected to a stub `fpatan; round; jmp 6deb6d`.
The fall-through path moves the window just before the site. Each direct branch to the site
is retargeted: a `rel32` one by changing its displacement, a short one by moving it into a
stub as `rel32`. The original `fpatan` is then unreachable. If the site were referenced
from data, or reached by a `call`, the generator would refuse and report it.

## Tools

* `patcher/cnc3_detfp.py` is what users run. Standard library only. It identifies the file
  by SHA-1, applies the bundled byte diff from `patcher/patches/*.json`, verifies the SHA-1
  of the result, and keeps the original as `*.orig`. For an unknown file it calls the
  generator.
* `patcher/detfp_gen.py` disassembles the executable (capstone), finds the sites and
  builds the patched file (pefile). It is deterministic. The bundled diffs were made with
  `cnc3_detfp.py --make-diff original patched out.json "name"`.

* `patcher/verify_patch.py` checks the result, see below.

`python3 patcher/detfp_gen.py original.dat patched.dat -v` prints every window it moved.

## Verifying the result

`patcher/verify_patch.py original.dat patched.dat` checks a patched file independently of
the generator. For every change in `.text` it decodes the stub and compares it instruction
by instruction with the displaced original code: same bytes, or for branches the same
target. It checks that rounding follows every transcendental and that the stub returns to
the right address. At the end it checks that every transcendental of the original is
rounded on every path that reaches it.

```
TW 1.9: 221 stubs checked, 220/220 transcendentals rounded, 0 problems
KW 1.2: 243 stubs checked, 242/242 transcendentals rounded, 0 problems
```

The rounding sequence leaves `EFLAGS` alone (`lea` instead of `sub`/`add`), keeps the x87
stack depth (`fstp` then `fld`), and only uses the 4 bytes just below `ESP`.

The executables have no relocation table (relocations stripped) and always load at
`0x400000`, so absolute addresses copied into stubs stay valid.

## Limitations

* Only the main executable is patched. Differences in DLLs, like Microsoft's vs Wine's
  `d3dx9_29.dll`, are not fixed by the patch and have to be avoided by configuration.
* Code generated at runtime would not be covered. The game has none that we know of.
