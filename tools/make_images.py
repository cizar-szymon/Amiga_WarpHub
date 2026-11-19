#!/usr/bin/env python3
"""Make catalog/images/<key>.png: the picture of every game on its tile in
WARP HUB (224x140, 256 colours, read on the Amiga by png.datatype).

    python tools/make_images.py

The source of each picture is "image" in catalog/games.json:
    {"src": "<path from AmigaGAMES>", "crop": [x0, y0, x1, y1], "pixel": true}
crop is optional; pixel: scale by whole steps without smoothing (pixel art).
A game without "image" gets a plain tile with its name.
Needs Pillow (PC only).
"""
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

W, H = 224, 140
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMES_DIR = os.path.dirname(HERE)          # C:\Workprojects\AmigaGAMES


def fit(im, pixel):
    """im scaled into W x H, centred on black."""
    out = Image.new('RGB', (W, H), (0, 0, 0))
    if pixel:
        k = max(1, min(W // im.width, H // im.height))
        im = im.resize((im.width * k, im.height * k), Image.NEAREST)
    else:
        k = min(W / im.width, H / im.height)
        im = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))),
                       Image.LANCZOS)
    out.paste(im, ((W - im.width) // 2, (H - im.height) // 2))
    return out


def plain(name):
    im = Image.new('RGB', (W, H), (24, 28, 40))
    d = ImageDraw.Draw(im)
    try:
        font = ImageFont.truetype('arialbd.ttf', 20)
    except OSError:
        font = ImageFont.load_default()
    box = d.textbbox((0, 0), name, font=font)
    d.text(((W - box[2]) // 2, (H - box[3]) // 2), name, fill=(230, 230, 240), font=font)
    return im


def main():
    with open(os.path.join(HERE, 'catalog', 'games.json'), encoding='utf-8') as f:
        games = json.load(f)['games']
    out_dir = os.path.join(HERE, 'catalog', 'images')
    os.makedirs(out_dir, exist_ok=True)
    for g in games:
        spec = g.get('image')
        if spec:
            im = Image.open(os.path.join(GAMES_DIR, spec['src'])).convert('RGB')
            if spec.get('crop'):
                im = im.crop(tuple(spec['crop']))
            im = fit(im, spec.get('pixel', False))
        else:
            im = plain(g['name']['en'])
        im = im.quantize(256, method=Image.MEDIANCUT, dither=Image.NONE)
        path = os.path.join(out_dir, g['key'] + '.png')
        im.save(path, optimize=True)
        print('%s: %d bytes' % (path, os.path.getsize(path)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
