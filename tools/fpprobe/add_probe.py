import struct, subprocess, sys, pefile
src, dst, hook_va, back_va = sys.argv[1], sys.argv[2], int(sys.argv[3], 16), int(sys.argv[4], 16)
pe = pefile.PE(src); base = pe.OPTIONAL_HEADER.ImageBase
sa, fa = pe.OPTIONAL_HEADER.SectionAlignment, pe.OPTIONAL_HEADER.FileAlignment
last = pe.sections[-1]
rva = (last.VirtualAddress + last.Misc_VirtualSize + sa - 1) // sa * sa
va = base + rva
tbl_n, table, code_va = va, va + 16, va + 0x1000
s = open('probe.s').read().replace('0x80927a', hex(back_va))
open('probe_gen.s', 'w').write(s)
subprocess.check_call(['i686-w64-mingw32-as', '--defsym', f'TABLE={table}', '--defsym', f'TABLE_N={tbl_n}', 'probe_gen.s', '-o', 'probe.o'])
subprocess.check_call(['i686-w64-mingw32-objcopy', '-O', 'binary', '-j', '.text', 'probe.o', 'probe.bin'])
code = open('probe.bin', 'rb').read()
body = bytearray(0x1000) + code
raw_size = (len(body) + fa - 1) // fa * fa
data = bytearray(open(src, 'rb').read())
raw_ptr = (len(data) + fa - 1) // fa * fa
data += b'\0' * (raw_ptr - len(data)) + body + b'\0' * (raw_size - len(body))
hdr = last.get_file_offset() + 40
assert not any(data[hdr:hdr + 40])
data[hdr:hdr + 40] = struct.pack('<8sIIIIIIHHI', b'.fpprobe', len(body), rva, raw_size, raw_ptr, 0, 0, 0, 0, 0xE0000020)
struct.pack_into('<H', data, pe.FILE_HEADER.get_file_offset() + 2, pe.FILE_HEADER.NumberOfSections + 1)
struct.pack_into('<I', data, pe.OPTIONAL_HEADER.get_file_offset() + 56, rva + (len(body) + sa - 1) // sa * sa)
text = pe.sections[0]
off = text.PointerToRawData + (hook_va - base - text.VirtualAddress)
assert data[off:off + 6] == b'\xff\x15\x60\x54\xa2\x00', data[off:off + 6].hex()
data[off:off + 6] = b'\xe9' + struct.pack('<i', code_va - (hook_va + 5)) + b'\x90'
open(dst, 'wb').write(data)
print(f'table at {table:#x} (count at {tbl_n:#x}), probe code at {code_va:#x}, {len(code)} bytes')
