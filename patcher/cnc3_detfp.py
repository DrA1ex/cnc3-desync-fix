#!/usr/bin/env python3
"""
cnc3_detfp - makes Command & Conquer 3 x87 math identical on Intel and on Apple Silicon (Rosetta 2).

    python3 cnc3_detfp.py <game folder or .dat file>             patch (keeps a .orig backup)
    python3 cnc3_detfp.py --status  <game folder or .dat file>   show what is installed
    python3 cnc3_detfp.py --restore <game folder or .dat file>   put the original back

Known executables are patched from the bundled patches/*.json (Python standard library only).
Unknown builds are patched by detfp_gen.py, which needs `pip install capstone pefile`.

Dev: python3 cnc3_detfp.py --make-diff <original> <patched> <out.json> "<name>"
"""
import argparse
import glob
import hashlib
import json
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PATCH_DIR = os.path.join(HERE, 'patches')
EXE_NAMES = ('cnc3game.dat', 'cnc3ep1.dat')


def sha1(path):
    h = hashlib.sha1()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def load_patches():
    patches = []
    for p in sorted(glob.glob(os.path.join(PATCH_DIR, '*.json'))):
        with open(p) as f:
            patches.append(json.load(f))
    return patches


def find_targets(path):
    if os.path.isfile(path):
        return [path]
    found = []
    for root, _, files in os.walk(path):
        found += [os.path.join(root, f) for f in files if f.lower() in EXE_NAMES]
    return sorted(found)


def apply_diff(src, patch, dst):
    with open(src, 'rb') as f:
        data = bytearray(f.read())
    data.extend(b'\0' * (patch['out_size'] - len(data)))
    for off, hexbytes in patch['chunks']:
        chunk = bytes.fromhex(hexbytes)
        data[off:off + len(chunk)] = chunk
    with open(dst, 'wb') as f:
        f.write(data)


def make_diff(orig, patched, out, name):
    a = open(orig, 'rb').read()
    b = open(patched, 'rb').read()
    a_ext = a + b'\0' * (len(b) - len(a))
    chunks, i = [], 0
    while i < len(b):
        if a_ext[i] == b[i]:
            i += 1
            continue
        j = i
        while j < len(b) and (a_ext[j] != b[j] or b[j:j + 8] != a_ext[j:j + 8]):
            j += 1
        chunks.append([i, b[i:j].hex()])
        i = j
    doc = {'name': name, 'in_sha1': hashlib.sha1(a).hexdigest(), 'in_size': len(a),
           'out_sha1': hashlib.sha1(b).hexdigest(), 'out_size': len(b), 'chunks': chunks}
    with open(out, 'w') as f:
        json.dump(doc, f, indent=0)
    print(f'{out}: {len(chunks)} chunks, {sum(len(c[1]) // 2 for c in chunks)} bytes')


def generate(src, dst):
    try:
        sys.path.insert(0, HERE)
        import detfp_gen
    except ImportError as e:
        sys.exit(f'unknown executable and the generator cannot run ({e}).\n'
                 f'Install its dependencies: python3 -m pip install capstone pefile')
    detfp_gen.main(src, dst)


def describe(path, patches):
    h = sha1(path)
    for p in patches:
        if h == p['in_sha1']:
            return 'original', p
        if h == p['out_sha1']:
            return 'patched', p
    return 'unknown', None


def patch_one(path, patches):
    backup = path + '.orig'
    state, p = describe(path, patches)
    if state == 'patched':
        print(f'{path}: already patched ({p["name"]})')
        return
    if os.path.exists(backup):
        src_state, p = describe(backup, patches)
        src = backup
        if state == 'unknown' and src_state == 'unknown' and sha1(backup) != sha1(path):
            sys.exit(f'{path}: neither the file nor {backup} is a known build, and they differ. '
                     f'Remove whichever is not the original and run again.')
    else:
        src_state, src = state, path
        shutil.copy2(path, backup)
        print(f'{path}: original saved as {os.path.basename(backup)}')
    with tempfile.NamedTemporaryFile(dir=os.path.dirname(path), delete=False) as t:
        tmp = t.name
    try:
        if p and src_state == 'original':
            apply_diff(src, p, tmp)
            if sha1(tmp) != p['out_sha1']:
                sys.exit(f'{path}: result checksum mismatch, nothing changed')
            how = p['name']
        else:
            generate(src, tmp)
            how = 'generated for an unknown build'
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)
    print(f'{path}: patched ({how}), sha1 {sha1(path)}')


def restore_one(path):
    backup = path + '.orig'
    if not os.path.exists(backup):
        print(f'{path}: no {os.path.basename(backup)}, nothing to restore')
        return
    shutil.copy2(backup, path)
    print(f'{path}: original restored')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group()
    g.add_argument('--status', action='store_true')
    g.add_argument('--restore', action='store_true')
    g.add_argument('--make-diff', nargs=4, metavar=('ORIG', 'PATCHED', 'OUT', 'NAME'))
    ap.add_argument('path', nargs='?')
    args = ap.parse_args()
    if args.make_diff:
        make_diff(*args.make_diff)
        return
    if not args.path:
        ap.error('give the game folder or the path to cnc3game.dat / cnc3ep1.dat')
    targets = find_targets(args.path)
    if not targets:
        sys.exit(f'no {" or ".join(EXE_NAMES)} under {args.path}')
    patches = load_patches()
    for t in targets:
        if args.status:
            state, p = describe(t, patches)
            print(f'{t}: {state}' + (f' ({p["name"]})' if p else ''))
        elif args.restore:
            restore_one(t)
        else:
            patch_one(t, patches)


if __name__ == '__main__':
    main()
