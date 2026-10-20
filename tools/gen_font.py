#!/usr/bin/env python3
"""Minimal PCF font reader. Converts the Shinonome 16px fonts into a C++ include
with 1-bit glyph bitmaps indexed by JIS code (half-width: byte code, full-width:
JIS X 0208 row/cell).  Shinonome is public domain.

usage: tools/gen_font.py tools/fonts src/core/gen/font_data.inc"""
import struct, sys, gzip

PCF_PROPERTIES, PCF_METRICS, PCF_BITMAPS, PCF_BDF_ENCODINGS = 1, 4, 8, 32

def read_pcf(path):
    data = gzip.open(path).read() if path.endswith('.gz') else open(path, 'rb').read()
    assert data[:4] == b'\x01fcp'
    n = struct.unpack('<I', data[4:8])[0]
    toc = {}
    for i in range(n):
        t, fmt, size, off = struct.unpack('<IIII', data[8 + 16 * i: 24 + 16 * i])
        toc[t] = (fmt, size, off)

    def endian(fmt):
        return '>' if fmt & 4 else '<'

    # properties
    fmt, size, off = toc[PCF_PROPERTIES]
    e = endian(struct.unpack('<I', data[off:off + 4])[0])
    pfmt = struct.unpack('<I', data[off:off + 4])[0]
    e = '>' if pfmt & 4 else '<'
    nprops = struct.unpack(e + 'I', data[off + 4: off + 8])[0]
    props_raw = []
    p = off + 8
    for i in range(nprops):
        name_off, is_str, val = struct.unpack(e + 'IBI', data[p:p + 9])
        props_raw.append((name_off, is_str, val))
        p += 9
    pad = (4 - (nprops & 3)) & 3
    p += pad
    strsize = struct.unpack(e + 'I', data[p:p + 4])[0]
    p += 4
    strs = data[p:p + strsize]

    def cstr(o):
        return strs[o:strs.index(b'\0', o)].decode('latin1')
    props = {}
    for name_off, is_str, val in props_raw:
        props[cstr(name_off)] = cstr(val) if is_str else val

    # metrics
    fmt, size, off = toc[PCF_METRICS]
    mfmt = struct.unpack('<I', data[off:off + 4])[0]
    e = '>' if mfmt & 4 else '<'
    metrics = []
    if mfmt & 0x100:  # compressed
        cnt = struct.unpack(e + 'H', data[off + 4:off + 6])[0]
        p = off + 6
        for i in range(cnt):
            lsb, rsb, w, asc, desc = [b - 0x80 for b in data[p:p + 5]]
            metrics.append((lsb, rsb, w, asc, desc))
            p += 5
    else:
        cnt = struct.unpack(e + 'I', data[off + 4:off + 8])[0]
        p = off + 8
        for i in range(cnt):
            lsb, rsb, w, asc, desc, attr = struct.unpack(e + 'hhhhhH', data[p:p + 12])
            metrics.append((lsb, rsb, w, asc, desc))
            p += 12

    # bitmaps
    fmt, size, off = toc[PCF_BITMAPS]
    bfmt = struct.unpack('<I', data[off:off + 4])[0]
    e = '>' if bfmt & 4 else '<'
    msbit = bool(bfmt & 8)
    glyph_pad = 1 << (bfmt & 3)
    scan_unit = 1 << ((bfmt >> 4) & 3)
    cnt = struct.unpack(e + 'I', data[off + 4:off + 8])[0]
    offsets = struct.unpack(e + '%dI' % cnt, data[off + 8:off + 8 + 4 * cnt])
    p = off + 8 + 4 * cnt
    sizes = struct.unpack(e + '4I', data[p:p + 16])
    p += 16
    bmdata = data[p:p + sizes[bfmt & 3]]

    # encodings
    fmt, size, off = toc[PCF_BDF_ENCODINGS]
    efmt = struct.unpack('<I', data[off:off + 4])[0]
    e = '>' if efmt & 4 else '<'
    min_b2, max_b2, min_b1, max_b1, default = struct.unpack(e + '5H', data[off + 4:off + 14])
    ncodes = (max_b2 - min_b2 + 1) * (max_b1 - min_b1 + 1)
    idx = struct.unpack(e + '%dH' % ncodes, data[off + 14:off + 14 + 2 * ncodes])
    enc = {}
    k = 0
    for b1 in range(min_b1, max_b1 + 1):
        for b2 in range(min_b2, max_b2 + 1):
            g = idx[k]
            k += 1
            if g != 0xFFFF:
                enc[(b1 << 8) | b2] = g

    def glyph_bits(g):
        lsb, rsb, w, asc, desc = metrics[g]
        gw = rsb - lsb
        gh = asc + desc
        stride = ((gw + glyph_pad * 8 - 1) // (glyph_pad * 8)) * glyph_pad
        base = offsets[g]
        rows = []
        for y in range(gh):
            row = bytearray(bmdata[base + y * stride: base + (y + 1) * stride])
            # byte order within scan unit
            if (bfmt & 4) == 0 and scan_unit > 1:  # LSByte first -> swap units
                for u in range(0, len(row), scan_unit):
                    row[u:u + scan_unit] = row[u:u + scan_unit][::-1]
            bits = []
            for x in range(gw):
                byte = row[x // 8]
                bit = (byte >> (7 - (x % 8))) & 1 if msbit else (byte >> (x % 8)) & 1
                bits.append(bit)
            rows.append(bits)
        return (lsb, rsb, w, asc, desc, rows)

    return props, enc, glyph_bits


def cell(glyph, cw, ascent, height):
    """Render a glyph into a cw x height cell (baseline at `ascent`)."""
    lsb, rsb, w, asc, desc, rows = glyph
    out = [[0] * cw for _ in range(height)]
    for y, bits in enumerate(rows):
        yy = ascent - asc + y
        if not 0 <= yy < height:
            continue
        for x, b in enumerate(bits):
            xx = lsb + x
            if b and 0 <= xx < cw:
                out[yy][xx] = 1
    return out


def main():
    src = sys.argv[1]
    out = sys.argv[2]
    pr, encr, gr = read_pcf(src + '/shnm8x16r.pcf.gz')
    pa, enca, ga = read_pcf(src + '/shnm8x16a.pcf.gz')
    pk, enck, gk = read_pcf(src + '/shnmk16.pcf.gz')
    asc = pk.get('FONT_ASCENT', 14)
    print('registry r:', pr.get('CHARSET_REGISTRY'), pr.get('CHARSET_ENCODING'), 'ascent', pr.get('FONT_ASCENT'))
    print('registry a:', pa.get('CHARSET_REGISTRY'), pa.get('CHARSET_ENCODING'), 'ascent', pa.get('FONT_ASCENT'))
    print('registry k:', pk.get('CHARSET_REGISTRY'), pk.get('CHARSET_ENCODING'), 'ascent', asc)
    H = 16
    half = {}
    # half-width: byte 0x20..0x7E from 'r' (ISO8859-1 / ascii); 0xA1..0xDF kana from 'a' (JISX0201)
    # 'r' is JIS X 0201 (roman with yen sign at 0x5C, plus half-width katakana)
    for code, g in encr.items():
        if 0x20 <= code <= 0x7E or 0xA1 <= code <= 0xDF:
            half[code] = cell(gr(g), 8, pr.get('FONT_ASCENT') or 14, H)
    full = {}
    for code, g in enck.items():
        full[code] = cell(gk(g), 16, asc, H)
    print('half glyphs', len(half), 'full glyphs', len(full))
    with open(out, 'w') as f:
        f.write('// Generated from the Shinonome 16px bitmap fonts (public domain).\n')
        f.write('// Half-width: 256 entries x 16 rows, one byte per row (MSB = left pixel).\n')
        f.write('// Full-width: JIS X 0208 rows 1..94 x cells 1..94, 16 rows x 2 bytes.\n')
        f.write('static const unsigned char kHalfGlyphs[256][16] = {\n')
        for c in range(256):
            rows = half.get(c)
            vals = []
            for y in range(H):
                v = 0
                if rows:
                    for x in range(8):
                        if rows[y][x]:
                            v |= 0x80 >> x
                vals.append(v)
            f.write('{' + ','.join(str(v) for v in vals) + '},\n')
        f.write('};\n')
        f.write('static const unsigned char kHalfPresent[256] = {' +
                ','.join('1' if c in half else '0' for c in range(256)) + '};\n')
        f.write('static const unsigned char kFullGlyphs[94*94][32] = {\n')
        present = []
        for row in range(1, 95):
            for col in range(1, 95):
                code = ((row + 0x20) << 8) | (col + 0x20)
                rows = full.get(code)
                present.append(1 if rows else 0)
                vals = []
                for y in range(H):
                    v = 0
                    if rows:
                        for x in range(16):
                            if rows[y][x]:
                                v |= 0x8000 >> x
                    vals.append(v >> 8)
                    vals.append(v & 0xFF)
                f.write('{' + ','.join(str(v) for v in vals) + '},\n')
        f.write('};\n')
        f.write('static const unsigned char kFullPresent[94*94] = {' + ','.join(map(str, present)) + '};\n')


if __name__ == '__main__':
    main()
