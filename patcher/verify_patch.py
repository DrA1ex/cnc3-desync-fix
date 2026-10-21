#!/usr/bin/env python3
"""
Independent check of a patched executable against the original (pip install capstone pefile).

    python3 verify_patch.py original.dat patched.dat

For every change in .text it decodes the stub the change jumps to and checks that the stub
runs exactly the displaced original instructions (same operands, same branch targets), plus
the float rounding after each transcendental, and returns to the right place.  Then it checks
that every transcendental of the original is rounded on every path that reaches it.
"""
import sys
import capstone
import pefile

TRANS = {'fsin', 'fcos', 'fsincos', 'fptan', 'fpatan', 'fyl2x'}
TWO = {'fsincos', 'fptan'}
ROUND = ['lea esp, [esp - 4]', 'fstp dword ptr [esp]', 'fld dword ptr [esp]', 'lea esp, [esp + 4]']


def load(path):
    pe = pefile.PE(path)
    base = pe.OPTIONAL_HEADER.ImageBase
    img = pe.get_memory_mapped_image()
    secs = {s.Name.rstrip(b'\0').decode(): (base + s.VirtualAddress, base + s.VirtualAddress + s.Misc_VirtualSize)
            for s in pe.sections}
    return base, img, secs


def main(orig, patched):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.skipdata = True
    base, oimg, osec = load(orig)
    _, pimg, psec = load(patched)
    tlo, thi = osec['.text']
    dlo, dhi = psec['.detfp']
    errors = []

    def insn_at(img, va):
        return next(md.disasm(img[va - base: va - base + 16], va), None)

    def text(i):
        return f'{i.mnemonic} {i.op_str}'.strip()

    def target(i):
        if (i.mnemonic.startswith('j') or i.mnemonic == 'call') and i.op_str.startswith('0x'):
            return int(i.op_str, 16)
        return None

    # original instruction boundaries
    otext = list(md.disasm(oimg[tlo - base: thi - base], tlo))
    oat = {i.address: i for i in otext}
    sites = {i.address for i in otext if i.mnemonic in TRANS}

    def is_redirect_stub(va, site):
        """stub at va == <original instruction at site>; round; jmp site+size"""
        o = oat[site]
        seq = list(md.disasm(pimg[va - base: va - base + 64], va))
        want = [text(o)] + ROUND + (['fxch st(1)'] + ROUND + ['fxch st(1)'] if o.mnemonic in TWO else [])
        got = [text(i) for i in seq[:len(want)]]
        nxt = seq[len(want)] if len(seq) > len(want) else None
        return got == want and nxt is not None and nxt.mnemonic == 'jmp' and target(nxt) == site + o.size

    def same_target(ot, pt):
        if ot == pt:
            return True
        return ot in sites and pt is not None and dlo <= pt < dhi and is_redirect_stub(pt, ot)

    rounded = set()
    checked_windows = 0
    changed = [va for va in range(tlo, thi) if oimg[va - base] != pimg[va - base]]
    seen = set()
    for va in changed:
        if va in seen:
            continue
        start = max(a for a in oat if a <= va) if va not in oat else va
        pi = insn_at(pimg, start)
        oi = oat[start]
        t = target(pi)
        if pi.mnemonic == 'jmp' and t is not None and dlo <= t < dhi and not (oi.mnemonic == 'jmp' and target(oi) in sites):
            # window: walk the stub and the original side by side
            stub = md.disasm(pimg[t - base: dhi - base], t)
            cur = start
            last_t = None
            for s in stub:
                st = text(s)
                if st == ROUND[0] and last_t is not None:
                    n = 4 if oat[last_t].mnemonic not in TWO else 10
                    want = ROUND if n == 4 else ROUND + ['fxch st(1)'] + ROUND + ['fxch st(1)']
                    block = [text(x) for x in md.disasm(pimg[s.address - base: s.address - base + 48], s.address)][:n]
                    if block != want:
                        errors.append(f'{start:#x}: broken rounding at {s.address:#x}')
                        break
                    rounded.add(last_t)
                    for _ in range(n - 1):
                        next(stub)
                    last_t = None
                    continue
                if s.mnemonic == 'jmp' and target(s) == cur:
                    break                                       # back to the instruction after the window
                if s.mnemonic == 'jmp' and target(s) is not None and not (cur in oat and oat[cur].mnemonic == 'jmp'):
                    tt = target(s)
                    if cur in sites and dlo <= tt < dhi and is_redirect_stub(tt, cur):
                        rounded.add(cur)                        # fall-through into a redirected site
                        break
                    errors.append(f'{start:#x}: stub jumps to {tt:#x}, expected {cur:#x}')
                    break
                o = oat.get(cur)
                if o is None:
                    errors.append(f'{start:#x}: stub runs past original instructions')
                    break
                if s.mnemonic != o.mnemonic and not (s.mnemonic == o.mnemonic.replace('short ', '')):
                    errors.append(f'{start:#x}: {text(s)} != original {text(o)} at {cur:#x}')
                    break
                ot, pt = target(o), target(s)
                if ot is not None or pt is not None:
                    if not same_target(ot, pt):
                        errors.append(f'{start:#x}: branch {text(o)} became {text(s)}')
                        break
                elif s.bytes != o.bytes:
                    errors.append(f'{start:#x}: bytes differ for {text(o)}')
                    break
                last_t = cur if o.mnemonic in TRANS else None
                cur += o.size
                if o.mnemonic == 'jmp' or o.mnemonic.startswith('ret'):
                    break
            # the rest of the window in .text must be nop
            for a in range(start + 5, cur):
                if pimg[a - base] != 0x90:
                    errors.append(f'{start:#x}: byte {a:#x} is not nop')
                    break
            for a in range(start, cur):
                seen.add(a)
            checked_windows += 1
        else:
            # a retargeted branch into a redirected site
            if not (oi.mnemonic == pi.mnemonic and same_target(target(oi), target(pi))):
                errors.append(f'{start:#x}: unexpected change {text(oi)} -> {text(pi)}')
            else:
                rounded.add(target(oi))
            for a in range(start, start + oi.size):
                seen.add(a)

    # every site: rounded, and if its own bytes are unchanged it must be unreachable
    for a in sorted(sites):
        if a not in rounded:
            errors.append(f'{a:#x} {oat[a].mnemonic}: not rounded')
        if all(oimg[x - base] == pimg[x - base] for x in range(a, a + 2)):
            ptext = {i.address: i for i in md.disasm(pimg[a - base - 16: a - base + 2], a - 16)}
            for i in md.disasm(pimg[tlo - base: thi - base], tlo):
                if target(i) == a:
                    errors.append(f'{a:#x}: still reached by {i.address:#x}')
    print(f'{checked_windows} stubs checked, {len(rounded & sites)}/{len(sites)} transcendentals rounded, '
          f'{len(errors)} problems')
    for e in errors[:50]:
        print('  ' + e)
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))
