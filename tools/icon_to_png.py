#!/usr/bin/env python3
"""Picture of an OS 3.5+ ColorIcon (FORM ICON: FACE + IMAG) as PNG, to look at
an icon on the PC.  python tools/icon_to_png.py IN.info OUT.png [image 1|2]"""
import struct
import sys

from PIL import Image


def unpack(data, count, depth):
    """ColorIcon RLE (byterun1 over a bit stream of depth-bit values)."""
    bits = []
    for b in data:
        for i in range(7, -1, -1):
            bits.append((b >> i) & 1)
    pos = 0

    def take(n):
        nonlocal pos
        v = 0
        for _ in range(n):
            v = (v << 1) | (bits[pos] if pos < len(bits) else 0)
            pos += 1
        return v

    out = []
    while len(out) < count and pos < len(bits):
        n = take(8)
        if n < 128:
            for _ in range(n + 1):
                out.append(take(depth))
        elif n > 128:
            v = take(depth)
            out.extend([v] * (257 - n))
    return out[:count]


def main():
    d = open(sys.argv[1], 'rb').read()
    which = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    i = d.find(b'FORM')
    pos = i + 12
    w = h = 0
    imags = []
    while pos < len(d) - 8:
        name, size = d[pos:pos + 4], struct.unpack('>I', d[pos + 4:pos + 8])[0]
        body = d[pos + 8:pos + 8 + size]
        if name == b'FACE':
            w, h = body[0] + 1, body[1] + 1
        elif name == b'IMAG':
            imags.append(body)
        pos += 8 + size + (size & 1)
    body = imags[which - 1]
    trans, ncol, flags, cmp, palcmp, depth = body[0], body[1] + 1, body[2], body[3], body[4], body[5]
    isize, psize = struct.unpack('>HH', body[6:10])
    pix = body[10:10 + isize + 1]
    pal = body[10 + isize + 1:]
    idx = unpack(pix, w * h, depth) if cmp else list(pix[:w * h])
    if flags & 2:
        rgb = unpack(pal, ncol * 3, 8) if palcmp else list(pal[:ncol * 3])
    else:
        rgb = [0] * (ncol * 3)
    img = Image.new('RGBA', (w, h))
    for k, v in enumerate(idx):
        r, g, b = rgb[v * 3:v * 3 + 3] if v * 3 + 2 < len(rgb) else (0, 0, 0)
        img.putpixel((k % w, k // w), (r, g, b, 0 if (flags & 1 and v == trans) else 255))
    img.save(sys.argv[2])
    print('%dx%d, %d colours, depth %d' % (w, h, ncol, depth))


if __name__ == '__main__':
    main()
