#!/usr/bin/env python3
"""The program icon of WARP HUB.

    python tools/make_hub_icon.py OUT.info [--preview OUT.png] [--stack N]
                                  [--style square|logo|drawer] [--height N]

square (the default, decision of the user 19.11.2026): 48x48 as the icons of
the WarpApps package (SYS:WarpApps), a black rounded square with the word
WARP of the WARP logo (amiga/icon/src/warp-logo-002.png, from amigawarp.eu)
on top and an orange rounded box with a black "HUB" below.
logo: the same side by side ("...hub" logo layout), 107x36.
drawer: the first version, in the style of the WARP drawer icon.

The icon has a classic 4 colour image (any Workbench, highlighted by
complement) and an OS 3.5+ ColorIcon with two images (normal, selected with a
yellow glow as the drawer icon), colour 0 transparent. ColorIcon writing as
Settlers/tools/make_icon.py.
"""
import argparse
import os
import struct
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from icon_to_png import unpack  # noqa: E402

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import paths  # noqa: E402
DRAWER = os.path.join(paths.GAMES, 'WARP-drawer.info')
LOGO = os.path.join(HERE, 'amiga', 'icon', 'src', 'warp-logo-002.png')
W = H = 46
ORANGE = (255, 163, 26)
FONT_FILE = r'C:\Windows\Fonts\ariblk.ttf'          # Arial Black

# 5x7 letters
FONT = {
    'H': ['#...#', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'],
    'U': ['#...#', '#...#', '#...#', '#...#', '#...#', '#...#', '.###.'],
    'B': ['####.', '#...#', '#...#', '####.', '#...#', '#...#', '####.'],
}
WHITE = (255, 255, 255, 255)
SHADOW = (24, 44, 84, 255)
RED = (222, 34, 34, 255)
RED_DARK = (110, 14, 14, 255)
GLOW = (255, 221, 0, 255)


def drawer_image(which=1):
    """the picture of the WARP drawer icon (RGBA)"""
    d = open(DRAWER, 'rb').read()
    pos = d.find(b'FORM') + 12
    imags, w, h = [], 0, 0
    while pos < len(d) - 8:
        name, size = d[pos:pos + 4], struct.unpack('>I', d[pos + 4:pos + 8])[0]
        body = d[pos + 8:pos + 8 + size]
        if name == b'FACE':
            w, h = body[0] + 1, body[1] + 1
        elif name == b'IMAG':
            imags.append(body)
        pos += 8 + size + (size & 1)
    body = imags[which - 1]
    trans, ncol, flags, cmp_, palcmp, depth = body[0], body[1] + 1, body[2], body[3], body[4], body[5]
    isize = struct.unpack('>H', body[6:8])[0]
    pix, pal = body[10:10 + isize + 1], body[10 + isize + 1:]
    idx = unpack(pix, w * h, depth) if cmp_ else list(pix[:w * h])
    rgb = unpack(pal, ncol * 3, 8) if palcmp else list(pal[:ncol * 3])
    img = Image.new('RGBA', (w, h))
    for k, v in enumerate(idx):
        alpha = 0 if (flags & 1 and v == trans) else 255
        img.putpixel((k % w, k // w), tuple(rgb[v * 3:v * 3 + 3]) + (alpha,))
    return img


def picture():
    src = drawer_image(1)
    img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    # the top of the drawer icon: the W on the panel
    for y in range(0, 23):
        for x in range(W):
            img.putpixel((x, y), src.getpixel((x, y)))
    # the body below: each row as row 30 of the drawer (plain blue with the
    # shading of its edges); the bottom edge and shadow as in the drawer
    top, bottom = src.getpixel((8, 22)), src.getpixel((8, 38))
    for y in range(23, 40):
        t = (y - 23) / 16.0
        fill = tuple(round(top[k] + (bottom[k] - top[k]) * t) for k in range(3)) + (255,)
        for x in range(W):
            p = src.getpixel((x, 30))
            inner = 6 <= x <= 38
            img.putpixel((x, y), fill if inner and p[3] else p)
    for y in range(40, H):
        for x in range(W):
            img.putpixel((x, y), src.getpixel((x, y)))

    # "HUB"
    text, x0, y0 = 'HUB', 14, 23
    for dx, dy, col in ((1, 1, SHADOW), (0, 0, WHITE)):
        x = x0
        for ch in text:
            for r, row in enumerate(FONT[ch]):
                for c, v in enumerate(row):
                    if v == '#':
                        img.putpixel((x + c + dx, y0 + r + dy), col)
            x += 6

    # the download arrow: shaft and head, outlined dark
    cx = 22                                  # left column of the 4 wide shaft
    shape = []
    for y in range(32, 35):
        shape += [(x, y) for x in range(cx, cx + 4)]
    for i, y in enumerate(range(35, 39)):
        shape += [(x, y) for x in range(cx - 3 + i, cx + 7 - i)]
    pts = set(shape)
    for (x, y) in pts:
        for ox, oy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1)):
            q = (x + ox, y + oy)
            if q not in pts and img.getpixel(q)[3]:
                img.putpixel(q, RED_DARK)
    for p in pts:
        img.putpixel(p, RED)
    return img


def picture_logo(height):
    """[WARP][HUB]: drawn 8 times larger, then scaled down"""
    from PIL import ImageDraw, ImageFont
    k = 8
    hh = height * k
    # the word WARP of the logo (rows 964..1312 of the 1537x1536 picture):
    # its letters as alpha by brightness over the dark field of the logo
    src = Image.open(LOGO).convert('RGBA')
    word = src.crop((0, 940, src.width, 1330))
    mask = Image.new('L', word.size, 0)
    px, mp = word.load(), mask.load()
    for y in range(word.height):
        for x in range(word.width):
            r, g, b, a = px[x, y]
            v = max(r, g, b)
            mp[x, y] = 0 if a == 0 else max(0, min(255, (v - 45) * 255 // 120))
    box = mask.getbbox()
    word, mask = word.crop(box), mask.crop(box)
    word.putalpha(mask)

    font = ImageFont.truetype(FONT_FILE, int(hh * 0.50))
    probe = ImageDraw.Draw(Image.new('L', (10, 10)))
    tb = probe.textbbox((0, 0), 'HUB', font=font)
    cap = tb[3] - tb[1]                      # height of the capitals
    ww = round(word.width * cap / word.height)
    word = word.resize((ww, cap), Image.LANCZOS)

    pad, gap, inner = int(hh * 0.12), int(hh * 0.09), int(hh * 0.10)
    box_w = (tb[2] - tb[0]) + 2 * inner
    box_h = cap + 2 * int(hh * 0.15)
    width = pad + ww + gap + box_w + pad
    big = Image.new('RGBA', (width, hh), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    d.rounded_rectangle((0, 0, width - 1, hh - 1), radius=int(hh * 0.18), fill=(0, 0, 0, 255))
    top = (hh - cap) // 2
    big.alpha_composite(word, (pad, top))
    bx, by = pad + ww + gap, (hh - box_h) // 2
    d.rounded_rectangle((bx, by, bx + box_w, by + box_h), radius=int(hh * 0.12),
                        fill=ORANGE + (255,))
    d.text((bx + inner - tb[0], top - tb[1]), 'HUB', font=font, fill=(0, 0, 0, 255))

    small = big.resize((round(width / k), height), Image.LANCZOS)
    # binary transparency (the icon has one transparent colour), at most 64
    # colours for the opaque pixels
    rgb = Image.new('RGB', small.size, (0, 0, 0))
    rgb.paste(small, (0, 0), small)
    rgb = rgb.quantize(64, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).convert('RGB')
    out = Image.new('RGBA', small.size, (0, 0, 0, 0))
    a = small.getchannel('A').load()
    for y in range(small.height):
        for x in range(small.width):
            if a[x, y] >= 128:
                out.putpixel((x, y), rgb.getpixel((x, y)) + (255,))
    return out


def warp_word():
    """the word WARP of the logo with alpha from the brightness of its letters"""
    src = Image.open(LOGO).convert('RGBA')
    word = src.crop((0, 940, src.width, 1330))
    mask = Image.new('L', word.size, 0)
    px, mp = word.load(), mask.load()
    for y in range(word.height):
        for x in range(word.width):
            r, g, b, a = px[x, y]
            mp[x, y] = 0 if a == 0 else max(0, min(255, (max(r, g, b) - 45) * 255 // 120))
    box = mask.getbbox()
    word, mask = word.crop(box), mask.crop(box)
    word.putalpha(mask)
    return word


def finish(big, w, h):
    """scaled down; one transparent colour, at most 64 colours"""
    small = big.resize((w, h), Image.LANCZOS)
    rgb = Image.new('RGB', small.size, (0, 0, 0))
    rgb.paste(small, (0, 0), small)
    rgb = rgb.quantize(64, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).convert('RGB')
    out = Image.new('RGBA', small.size, (0, 0, 0, 0))
    a = small.getchannel('A').load()
    for y in range(small.height):
        for x in range(small.width):
            if a[x, y] >= 128:
                out.putpixel((x, y), rgb.getpixel((x, y)) + (255,))
    return out


def picture_square(size):
    """WARP over HUB on a black rounded square, drawn 8 times larger"""
    from PIL import ImageDraw, ImageFont
    k = 8
    n = size * k
    big = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    edge = int(n * 0.03)
    d.rounded_rectangle((0, 0, n - 1, n - 1), radius=int(n * 0.16), fill=(70, 70, 76, 255))
    d.rounded_rectangle((edge, edge, n - 1 - edge, n - 1 - edge), radius=int(n * 0.14),
                        fill=(0, 0, 0, 255))
    inner_w = n - 2 * int(n * 0.12)
    # WARP: as wide as the inside
    word = warp_word()
    ww = inner_w
    wh = round(word.height * ww / word.width)
    word = word.resize((ww, wh), Image.LANCZOS)
    # HUB box: as wide as the inside too
    font = ImageFont.truetype(FONT_FILE, int(n * 0.24))
    tb = d.textbbox((0, 0), 'HUB', font=font)
    tw, th = tb[2] - tb[0], tb[3] - tb[1]
    box_h = th + int(n * 0.10)
    box_w = tw + int(n * 0.14)           # the box only around the word
    gap = int(n * 0.07)
    total = wh + gap + box_h
    y0 = (n - total) // 2
    x0 = (n - ww) // 2
    big.alpha_composite(word, (x0, y0))
    by = y0 + wh + gap
    bx = (n - box_w) // 2
    d.rounded_rectangle((bx, by, bx + box_w, by + box_h), radius=int(n * 0.06), fill=ORANGE + (255,))
    d.text(((n - tw) // 2 - tb[0], by + (box_h - th) // 2 - tb[1]), 'HUB', font=font,
           fill=(0, 0, 0, 255))
    return finish(big, size, size)


def picture_tool(kind, size):
    """the network tools, in the style of the square: netmon = bar chart in the
    colours of its servers + NET; nettest = signal bars + a green tick + TEST"""
    from PIL import ImageDraw, ImageFont
    k = 8
    n = size * k
    big = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    edge = int(n * 0.03)
    d.rounded_rectangle((0, 0, n - 1, n - 1), radius=int(n * 0.16), fill=(70, 70, 76, 255))
    d.rounded_rectangle((edge, edge, n - 1 - edge, n - 1 - edge), radius=int(n * 0.14),
                        fill=(0, 0, 0, 255))
    font = ImageFont.truetype(FONT_FILE, int(n * 0.2))
    word = 'NET' if kind == 'netmon' else 'TEST'
    tb = d.textbbox((0, 0), word, font=font)
    tw, th = tb[2] - tb[0], tb[3] - tb[1]
    box_h = th + int(n * 0.08)
    box_w = tw + int(n * 0.12)
    by = n - int(n * 0.12) - box_h
    bx = (n - box_w) // 2
    d.rounded_rectangle((bx, by, bx + box_w, by + box_h), radius=int(n * 0.05), fill=ORANGE + (255,))
    d.text(((n - tw) // 2 - tb[0], by + (box_h - th) // 2 - tb[1]), word, font=font, fill=(0, 0, 0, 255))
    # the chart above the word
    left, right = int(n * 0.14), n - int(n * 0.14)
    bottom, top = by - int(n * 0.06), int(n * 0.12)
    if kind == 'netmon':
        cols = [(48, 112, 240), (240, 144, 32), (32, 176, 80), (192, 64, 208)]
        heights = [0.55, 0.7, 0.62, 0.78, 0.5, 0.86, 0.66, 0.74]
        bw = (right - left) / len(heights)
        for i, h in enumerate(heights):
            x0 = int(left + i * bw + bw * 0.12)
            x1 = int(left + (i + 1) * bw - bw * 0.12)
            d.rectangle((x0, int(bottom - (bottom - top) * h), x1, bottom), fill=cols[i // 2] + (255,))
        d.line((left, bottom + int(n * 0.012), right, bottom + int(n * 0.012)), fill=(150, 150, 150, 255),
               width=int(n * 0.02))
    else:
        bw = (right - left) / 5
        for i in range(4):
            x0 = int(left + i * bw + bw * 0.15)
            x1 = int(left + (i + 1) * bw - bw * 0.15)
            h = 0.3 + 0.2 * i
            d.rectangle((x0, int(bottom - (bottom - top) * h), x1, bottom), fill=(48, 112, 240, 255))
        # the tick
        w = int(n * 0.07)
        pts = [(left + bw * 3.7, top + (bottom - top) * 0.55), (left + bw * 4.25, top + (bottom - top) * 0.85),
               (right, top)]
        d.line(pts, fill=(40, 210, 70, 255), width=w, joint='curve')
    return finish(big, size, size)


def selected(img):
    """darker, with a yellow glow around it (as the selected drawer icon)"""
    w, h = img.size
    out = Image.new('RGBA', img.size, (0, 0, 0, 0))
    for y in range(h):
        for x in range(w):
            r, g, b, a = img.getpixel((x, y))
            if a:
                out.putpixel((x, y), (r * 3 // 4, g * 3 // 4, b * 3 // 4, 255))
    for y in range(h):
        for x in range(w):
            if img.getpixel((x, y))[3]:
                continue
            near = any(0 <= x + dx < w and 0 <= y + dy < h and img.getpixel((x + dx, y + dy))[3]
                       for dx in (-1, 0, 1) for dy in (-1, 0, 1))
            if near:
                out.putpixel((x, y), GLOW)
    return out


# --- writing (as Settlers/tools/make_icon.py, plus transparency and 2 images) ---

def rle_pack(values, depth):
    out_bits = []

    def emit(v, n):
        for i in range(n - 1, -1, -1):
            out_bits.append((v >> i) & 1)

    i = 0
    while i < len(values):
        run = 1
        while i + run < len(values) and values[i + run] == values[i] and run < 128:
            run += 1
        if run >= 3:
            emit(257 - run, 8)
            emit(values[i], depth)
            i += run
            continue
        lit = 0
        while i + lit < len(values) and lit < 128:
            r = 1
            while i + lit + r < len(values) and values[i + lit + r] == values[i + lit] and r < 3:
                r += 1
            if r >= 3:
                break
            lit += 1
        emit(lit - 1, 8)
        for v in values[i:i + lit]:
            emit(v, depth)
        i += lit
    while len(out_bits) % 8:
        out_bits.append(0)
    return bytes(int(''.join(map(str, out_bits[k:k + 8])), 2) for k in range(0, len(out_bits), 8))


def chunk(name, data):
    pad = b'\0' if len(data) & 1 else b''
    return name + struct.pack('>I', len(data)) + data + pad


def imag(img):
    """IMAG chunk: colour 0 transparent"""
    colors = [(0, 0, 0)]
    idx = []
    for r, g, b, a in img.getdata():
        if not a:
            idx.append(0)
            continue
        if (r, g, b) not in colors[1:]:
            colors.append((r, g, b))
        idx.append(colors.index((r, g, b), 1))
    if len(colors) > 256:
        raise SystemExit('more than 256 colours')
    depth = max(1, (len(colors) - 1).bit_length())
    pixels = rle_pack(idx, depth)
    palette = b''.join(struct.pack('>BBB', *c) for c in colors)
    return struct.pack('>BBBBBBHH', 0, len(colors) - 1, 0x03, 1, 0, depth, len(pixels) - 1,
                       len(palette) - 1) + pixels + palette, len(palette)


def color_icon(normal, sel):
    a, pa = imag(normal)
    b, pb = imag(sel)
    face = struct.pack('>BBBBH', W - 1, H - 1, 0, 0x11, max(pa, pb) - 1)
    body = b'ICON' + chunk(b'FACE', face) + chunk(b'IMAG', a) + chunk(b'IMAG', b)
    return b'FORM' + struct.pack('>I', len(body)) + body


WB4 = [(0xAA, 0xAA, 0xAA), (0x00, 0x00, 0x00), (0xFF, 0xFF, 0xFF), (0x66, 0x88, 0xBB)]


def planar(img):
    words = (W + 15) // 16
    px = list(img.getdata())

    def wb(p):
        if not p[3]:
            return 0
        return min(range(4), key=lambda i: sum((p[k] - WB4[i][k]) ** 2 for k in range(3)))

    planes = []
    for p in range(2):
        data = bytearray()
        for y in range(H):
            bits = 0
            for x in range(words * 16):
                v = wb(px[y * W + x]) if x < W else 0
                bits = (bits << 1) | ((v >> p) & 1)
            data += bits.to_bytes(words * 2, 'big')
        planes.append(bytes(data))
    return b''.join(planes)


def disk_object(normal, sel, stack):
    gadget = struct.pack('>IhhhhHHHIIIIIHI', 0, 0, 0, W, H + 1, 0x0004, 0x0003, 0x0001,
                         1, 0, 0, 0, 0, 0, 1)
    head = struct.pack('>HH', 0xE310, 1) + gadget + struct.pack(
        '>BBIIiiIII', 3, 0, 0, 0, -0x80000000, -0x80000000, 0, 0, stack)
    image = struct.pack('>hhhhhIBBI', 0, 0, W, H, 2, 1, 3, 0, 0) + planar(normal)
    return head + image + color_icon(normal, sel)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--preview')
    ap.add_argument('--stack', type=int, default=65536)
    ap.add_argument('--style', choices=('square', 'logo', 'drawer', 'netmon', 'nettest'),
                    default='square')
    ap.add_argument('--height', type=int, default=0, help='square: 46, logo: 34')
    args = ap.parse_args()
    global W, H
    if args.style in ('netmon', 'nettest'):
        normal = picture_tool(args.style, args.height or 46)
        framed = Image.new('RGBA', (normal.width + 2, normal.height + 2), (0, 0, 0, 0))
        framed.paste(normal, (1, 1))
        normal = framed
    elif args.style == 'square':
        normal = picture_square(args.height or 46)
        framed = Image.new('RGBA', (normal.width + 2, normal.height + 2), (0, 0, 0, 0))
        framed.paste(normal, (1, 1))
        normal = framed
    elif args.style == 'logo':
        args.height = args.height or 34
        normal = picture_logo(args.height)
        # the glow of the selected image needs a free pixel around
        framed = Image.new('RGBA', (normal.width + 2, normal.height + 2), (0, 0, 0, 0))
        framed.paste(normal, (1, 1))
        normal = framed
    else:
        normal = picture()
    W, H = normal.size
    sel = selected(normal)
    open(args.out, 'wb').write(disk_object(normal, sel, args.stack))
    if args.preview:
        bg = (0x88, 0x99, 0xAA, 255)
        sheet = Image.new('RGBA', (W * 2 * 6 + 24, H * 6 + 12), bg)
        for k, im in enumerate((normal, sel)):
            big = im.resize((W * 6, H * 6), Image.NEAREST)
            sheet.paste(big, (6 + k * (W * 6 + 12), 6), big)
        sheet.save(args.preview)
    print('%s: %dx%d, %d bytes' % (args.out, W, H, os.path.getsize(args.out)))


if __name__ == '__main__':
    main()
