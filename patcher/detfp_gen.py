#!/usr/bin/env python3
"""
detfp_gen: make C&C3 (TW cnc3game.dat / KW cnc3ep1.dat) x87 transcendentals cross-CPU deterministic.

Rosetta 2 and Intel hardware disagree in the last bit of the 80-bit result of
fsin/fcos/fsincos/fptan/fpatan/fyl2x (~5-18% of inputs), while the results rounded to
float32 are bit-identical (verified exhaustively).  The game sometimes keeps the 80-bit
result on the x87 stack and subtracts from it (e.g. atan2(dy,dx) - orientation), which
exposes that last bit.  This patch rounds every transcendental result to float32
immediately, by redirecting each site to a stub in a new section:

    <displaced instructions before>  T  <round ST0 (and ST1)>  <displaced after>  jmp back

A site that is itself a branch target with a branch target right after it cannot host a
5-byte jmp.  Then every way into it (fall-through and direct branches) is redirected to a
stub `T; round; jmp <after T>` instead, and the original instruction becomes dead code.

    python3 detfp_gen.py <original.dat> <patched.dat> [-v]      (pip install capstone pefile)
"""
import struct, sys
import capstone
import pefile

TRANS = {b'\xd9\xfe': 'fsin', b'\xd9\xff': 'fcos', b'\xd9\xfb': 'fsincos',
         b'\xd9\xf2': 'fptan', b'\xd9\xf3': 'fpatan', b'\xd9\xf1': 'fyl2x'}
TWO_RESULTS = {'fsincos', 'fptan'}
ROUND_ST0 = bytes.fromhex('8d6424fc' 'd91c24' 'd90424' '8d642404')  # lea esp,[esp-4]; fstp dword[esp]; fld dword[esp]; lea esp,[esp+4]
FXCH1 = bytes.fromhex('d9c9')


def rounding(name):
    if name in TWO_RESULTS:
        return ROUND_ST0 + FXCH1 + ROUND_ST0 + FXCH1
    return ROUND_ST0


def main(src, dst, verbose=False):
    pe = pefile.PE(src)
    base = pe.OPTIONAL_HEADER.ImageBase
    text = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text')
    tva = base + text.VirtualAddress
    tdata = text.get_data()[:text.Misc_VirtualSize]
    tend = tva + len(tdata)

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.skipdata = True
    insns = list(md.disasm(tdata, tva))
    by_addr = {i.address: k for k, i in enumerate(insns)}

    # every address something might jump to: direct branch targets + any dword in the image
    # that points into .text (switch tables, vtables, callbacks, SEH handlers)
    targets = set()
    branches_to = {}
    for i in insns:
        if i.mnemonic.startswith('j') or i.mnemonic == 'call' or i.mnemonic.startswith('loop'):
            if i.op_str.startswith('0x'):
                t = int(i.op_str, 16)
                targets.add(t)
                branches_to.setdefault(t, []).append(i)
    raw = open(src, 'rb').read()
    data_targets = set()
    for off in range(0, len(raw) - 3):
        v = struct.unpack_from('<I', raw, off)[0]
        if tva <= v < tend:
            data_targets.add(v)
    targets |= data_targets

    def is_rel(i):
        return i.mnemonic.startswith('j') or i.mnemonic == 'call' and i.bytes[0] == 0xE8 or i.mnemonic.startswith('loop')

    def relocatable(i):
        if i.mnemonic in ('db', ) or i.id == 0:
            return False
        if i.mnemonic.startswith('loop') or i.mnemonic == 'jecxz':
            return False
        if i.mnemonic.startswith('j') and not i.op_str.startswith('0x'):
            return i.mnemonic == 'jmp'  # indirect jmp is position independent
        return True

    def emit(i, at, tgt=None):
        """Encode instruction i to run at address `at` (optionally branching to `tgt`); returns bytes."""
        if is_rel(i) and i.op_str.startswith('0x'):
            tgt = int(i.op_str, 16) if tgt is None else tgt
            if i.mnemonic == 'call':
                return b'\xe8' + struct.pack('<i', tgt - (at + 5))
            if i.mnemonic == 'jmp':
                return b'\xe9' + struct.pack('<i', tgt - (at + 5))
            # jcc: short 7x or near 0F 8x
            cc = (i.bytes[0] & 0x0f) if i.bytes[0] in range(0x70, 0x80) else (i.bytes[1] & 0x0f)
            return bytes([0x0f, 0x80 | cc]) + struct.pack('<i', tgt - (at + 6))
        return bytes(i.bytes)

    covered = set()          # addresses of instructions displaced so far

    def find_window(k, before, after, interior_ok=lambda w: True):
        """Smallest window of >= 5 bytes around insns[k] that can be moved to a stub."""
        for b in before:
            for f in after:
                lo, hi = k - b, k + f + 1
                if lo < 0 or hi > len(insns) or hi <= lo:
                    continue
                w = insns[lo:hi]
                if sum(i.size for i in w) < 5:
                    continue
                if any(w[j].address + w[j].size != w[j + 1].address for j in range(len(w) - 1)):
                    continue
                if any(i.address in targets for i in w[1:]):
                    continue
                if any(i.address in covered for i in w):
                    continue
                if not all(relocatable(i) for i in w):
                    continue
                # a control transfer may only be the last displaced instruction
                if any(i.mnemonic.startswith('j') or i.mnemonic.startswith('ret') for i in w[:-1]):
                    continue
                if not interior_ok(w):
                    continue
                return lo, hi
        return None

    sites = [i for i in insns if bytes(i.bytes) in TRANS]
    windows = []             # (lo, hi, {insn address: new branch target or stub name})
    redirects = []           # (site insn, stub name)
    rel32_retarget = {}      # rel32 branch address -> stub name
    late_windows = []        # windows of redirected sites, laid out after the regular ones
    failed = []
    for s in sites:
        k = by_addr[s.address]
        if s.address in covered:
            continue
        chosen = find_window(k, range(0, 5), range(0, 6))
        if chosen:
            windows.append((chosen[0], chosen[1], {}))
            for i in insns[chosen[0]:chosen[1]]:
                covered.add(i.address)
            continue
        # fallback: redirect every way into the site to a dedicated stub
        stub = f'redirect_{s.address:x}'
        plan, ok = [], s.address not in data_targets
        prev = insns[k - 1] if k else None
        if ok and prev and not (prev.mnemonic == 'jmp' or prev.mnemonic.startswith('ret')):
            # fall-through: move a window that ends right before the site, then jump to the stub
            w = find_window(k - 1, range(0, 5), [0], lambda w: not any(i.mnemonic.startswith('j') for i in w))
            if w:
                plan.append(('window', w, {'__fallthrough__': stub}))
            else:
                ok = False
        for br in branches_to.get(s.address, []) if ok else []:
            if br.mnemonic == 'call':
                ok = False
            elif br.size >= 5:                       # rel32 jmp/jcc: just change the displacement
                plan.append(('rel32', br, stub))
            else:                                    # short branch: move it into a stub, re-encoded as rel32
                w = find_window(by_addr[br.address], range(0, 5), [0])
                if w:
                    plan.append(('window', w, {br.address: stub}))
                else:
                    ok = False
        if not ok:
            failed.append(s)
            continue
        for kind, a, b in plan:
            if kind == 'rel32':
                rel32_retarget[a.address] = b
                covered.add(a.address)
            else:
                late_windows.append((a[0], a[1], b))
                for i in insns[a[0]:a[1]]:
                    covered.add(i.address)
        redirects.append((s, stub))
    windows += late_windows

    # build stub section
    sec_align = pe.OPTIONAL_HEADER.SectionAlignment
    file_align = pe.OPTIONAL_HEADER.FileAlignment
    last = pe.sections[-1]
    new_rva = (last.VirtualAddress + last.Misc_VirtualSize + sec_align - 1) // sec_align * sec_align
    new_va = base + new_rva

    # layout pass: window stubs first, then redirect stubs; sizes do not depend on addresses
    def window_code(lo, hi, retarget, at, stub_va):
        w = insns[lo:hi]
        code = bytearray()
        names = []
        for i in w:
            tgt = retarget.get(i.address)
            code += emit(i, at + len(code), stub_va.get(tgt) if tgt else None)
            n = TRANS.get(bytes(i.bytes))
            if n:
                code += rounding(n)
                names.append(n)
        last_i = w[-1]
        if '__fallthrough__' in retarget:
            dest = stub_va.get(retarget['__fallthrough__'], 0)
        elif last_i.mnemonic == 'jmp' or last_i.mnemonic.startswith('ret'):
            dest = None
        else:
            dest = last_i.address + last_i.size
        if dest is not None:
            code += b'\xe9' + struct.pack('<i', dest - (at + len(code) + 5))
        return code, names

    def redirect_code(s, at):
        n = TRANS[bytes(s.bytes)]
        code = bytearray(s.bytes) + rounding(n)
        code += b'\xe9' + struct.pack('<i', s.address + s.size - (at + len(code) + 5))
        return code, [n]

    def align16(n):
        return (n + 15) // 16 * 16

    stub_va = {}
    off = 0
    offsets = []
    for lo, hi, rt in windows:
        offsets.append(off)
        off = align16(off + len(window_code(lo, hi, rt, 0, {})[0]))
    for s, name in redirects:
        stub_va[name] = new_va + off
        off = align16(off + len(redirect_code(s, 0)[0]))

    stub = bytearray()
    text_patch = {}
    report = []
    for (lo, hi, rt), o in zip(windows, offsets):
        at = new_va + o
        code, names = window_code(lo, hi, rt, at, stub_va)
        stub += code
        while len(stub) % 16:
            stub += b'\xcc'
        w = insns[lo:hi]
        wstart, wlen = w[0].address, sum(i.size for i in w)
        text_patch[wstart] = b'\xe9' + struct.pack('<i', at - (wstart + 5)) + b'\x90' * (wlen - 5)
        report.append((wstart, wlen, names, [f'{i.mnemonic} {i.op_str}' for i in w]))
    for s, name in redirects:
        assert new_va + len(stub) == stub_va[name]
        code, names = redirect_code(s, stub_va[name])
        stub += code
        while len(stub) % 16:
            stub += b'\xcc'
        report.append((s.address, 0, names, ['redirected: ' + f'{s.mnemonic}']))
    for addr, name in rel32_retarget.items():
        i = insns[by_addr[addr]]
        text_patch[addr] = emit(i, addr, stub_va[name])

    data = bytearray(raw)
    for va, patch in text_patch.items():
        o = text.PointerToRawData + (va - tva)
        data[o:o + len(patch)] = patch

    # append section header + raw data
    raw_size = (len(stub) + file_align - 1) // file_align * file_align
    raw_ptr = (len(data) + file_align - 1) // file_align * file_align
    data += b'\0' * (raw_ptr - len(data)) + bytes(stub) + b'\xcc' * (raw_size - len(stub))
    hdr_off = pe.sections[-1].get_file_offset() + 40
    if hdr_off + 40 > pe.OPTIONAL_HEADER.SizeOfHeaders or any(data[hdr_off:hdr_off + 40]):
        sys.exit('no room for a new section header')
    sh = struct.pack('<8sIIIIIIHHI', b'.detfp', len(stub), new_rva, raw_size, raw_ptr, 0, 0, 0, 0,
                     0x60000020)  # CODE | EXECUTE | READ
    data[hdr_off:hdr_off + 40] = sh
    fh = pe.FILE_HEADER.get_file_offset()
    struct.pack_into('<H', data, fh + 2, pe.FILE_HEADER.NumberOfSections + 1)
    oh = pe.OPTIONAL_HEADER.get_file_offset()
    struct.pack_into('<I', data, oh + 56, new_rva + (len(stub) + sec_align - 1) // sec_align * sec_align)  # SizeOfImage
    struct.pack_into('<I', data, oh + 64, 0)  # CheckSum
    open(dst, 'wb').write(data)

    n_sites = sum(len(r[2]) for r in report)
    print(f'sites found: {len(sites)}, patched: {n_sites} ({len(windows)} windows, '
          f'{len(redirects)} redirected), failed: {len(failed)}')
    for s in failed:
        print(f'  FAILED 0x{s.address:x} {s.mnemonic}')
    if verbose:
        for wstart, wlen, names, txt in report:
            print(f'0x{wstart:x} +{wlen}: {",".join(names)} | ' + '; '.join(txt))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], '-v' in sys.argv)
