#!/usr/bin/env python3
"""
Who calls this?  Walks the direct-call graph up from code addresses until it reaches
virtual methods of classes with RTTI, and prints the class names.  Used to tell whether
the game calls a D3DX function from simulation code or from rendering code.

    python3 xref_rtti.py cnc3game.dat 726637 477c54 ...      (pip install capstone pefile)

Heuristic: function starts are direct call targets and vtable entries, so a function that
is only reached through other function pointers shows up with callers=0.
"""
import bisect
import re
import struct
import sys
import pefile

DEPTH = 7

pe = pefile.PE(sys.argv[1])
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
text = next(s for s in pe.sections if s.Name.startswith(b'.text'))
tlo, thi = base + text.VirtualAddress, base + text.VirtualAddress + text.Misc_VirtualSize


def rd(va, n):
    return img[va - base: va - base + n]


def d32(va):
    b = rd(va, 4)
    return struct.unpack('<I', b)[0] if len(b) == 4 and va >= base else 0


# call graph from direct calls
tdata = rd(tlo, thi - tlo)
calls = {}  # target -> [call sites]
for m in re.finditer(rb'\xe8', tdata):
    o = m.start()
    if o + 5 > len(tdata):
        continue
    site = tlo + o
    tgt = (site + 5 + struct.unpack_from('<i', tdata, o + 1)[0]) & 0xffffffff
    if tlo <= tgt < thi:
        calls.setdefault(tgt, []).append(site)


def rtti(slot):
    """(class name, slot index) for a vtable slot, via the RTTI complete object locator."""
    p = slot
    while tlo <= d32(p - 4) < thi:
        p -= 4
    col = d32(p - 4)
    try:
        td = d32(col + 12)
        return rd(td + 8, 80).split(b'\0')[0].decode(), (slot - p) // 4
    except Exception:
        return None


vt = {}  # function -> vtable slots that point to it
for o in range(0, len(img) - 3, 4):
    v = struct.unpack_from('<I', img, o)[0]
    if tlo <= v < thi and not (text.VirtualAddress <= o < text.VirtualAddress + text.Misc_VirtualSize):
        vt.setdefault(v, []).append(base + o)
vt = {f: [s for s in sl if (rtti(s) or ('',))[0].startswith('.?A')] for f, sl in vt.items()}
vt = {f: sl for f, sl in vt.items() if sl}
funcs = sorted(set(calls) | set(vt))


def func_of(va):
    i = bisect.bisect_right(funcs, va) - 1
    return funcs[i] if i >= 0 else None


def describe(f, depth, seen, ind=''):
    if f in seen or depth < 0:
        return
    seen.add(f)
    names = [f'{n}[{k}]' for n, k in filter(None, (rtti(s) for s in vt.get(f, [])))]
    print(f'{ind}0x{f:x} vt={names[:4]} callers={len(calls.get(f, []))}')
    if names:
        return
    for c in calls.get(f, [])[:8]:
        describe(func_of(c), depth - 1, seen, ind + '  ')


for site in [int(x, 16) for x in sys.argv[2:]]:
    print(f'### site 0x{site:x}')
    describe(func_of(site), DEPTH, set())
