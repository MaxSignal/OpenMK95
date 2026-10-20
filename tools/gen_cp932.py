#!/usr/bin/env python3
"""Generate a CP932 <-> Unicode table (from Python's standard cp932 codec).

usage: tools/gen_cp932.py src/core/gen/cp932_table.inc"""
import sys
out = open(sys.argv[1], 'w')
out.write('// Generated from the Windows code page 932 mapping (Python cp932 codec).\n')
out.write('// kCp932Double[(lead-0x81 or lead-0xE0+31)][trail-0x40] -> UTF-16 code unit, 0 = undefined\n')
leads = list(range(0x81, 0xA0)) + list(range(0xE0, 0xFD))
out.write('static const unsigned short kCp932Double[%d][189] = {\n' % len(leads))
for lead in leads:
    vals = []
    for trail in range(0x40, 0xFD):
        try:
            u = bytes([lead, trail]).decode('cp932')
            vals.append(ord(u) if len(u) == 1 and ord(u) < 0x10000 else 0)
        except UnicodeDecodeError:
            vals.append(0)
    out.write('{' + ','.join(str(v) for v in vals) + '},\n')
out.write('};\n')
single = []
for b in range(256):
    try:
        u = bytes([b]).decode('cp932')
        single.append(ord(u))
    except UnicodeDecodeError:
        single.append(0)
out.write('static const unsigned short kCp932Single[256] = {' + ','.join(map(str, single)) + '};\n')
