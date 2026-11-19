#!/usr/bin/env python3
"""Make catalog/images/<key>.png: the picture of every game on its tile in
WARP HUB (224x140, 256 colours, read on the Amiga by png.datatype).

    python tools/make_images.py               from IMAGE-SOURCE of the game files
    python tools/make_images.py --normalize   only bring every catalog/images/*.png
                                              to 224x140 and 256 colours (GitHub
                                              Actions: pictures from the web editor)

IMAGE-SOURCE in catalog/games/<key>.txt: <path from AmigaGAMES> [x0 y0 x1 y1]
[pixel] - crop is optional; pixel: scale by whole steps without smoothing.
A game without a picture gets a plain tile with its name.
Needs Pillow.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import game_files  # noqa: E402

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


def save(im, path):
    im = im.convert('RGB').quantize(256, method=Image.MEDIANCUT, dither=Image.NONE)
    im.save(path, optimize=True)
    print('%s: %d bytes' % (path, os.path.getsize(path)))


def normalize(out_dir):
    """Every picture to W x H (scaled and centred, black around) and 256 colours;
    the ones already right stay as they are (no new commit every day)."""
    for fn in sorted(os.listdir(out_dir)):
        if not fn.endswith('.png'):
            continue
        path = os.path.join(out_dir, fn)
        im = Image.open(path)
        if im.size == (W, H) and im.mode == 'P':
            continue
        save(fit(im.convert('RGB'), False), path)


def main():
    out_dir = os.path.join(HERE, 'catalog', 'images')
    os.makedirs(out_dir, exist_ok=True)
    if '--normalize' in sys.argv[1:]:
        normalize(out_dir)
        return 0
    for g in game_files.load_games(os.path.join(HERE, 'catalog', 'games')):
        spec = g.get('image')
        path = os.path.join(out_dir, g['key'] + '.png')
        if spec:
            src = os.path.join(GAMES_DIR, spec['src'])
            if not os.path.exists(src):
                print('%s: no %s, picture kept' % (g['key'], src))
                continue
            im = Image.open(src).convert('RGB')
            if spec.get('crop'):
                im = im.crop(tuple(spec['crop']))
            im = fit(im, spec.get('pixel', False))
        elif os.path.exists(path):
            continue                      # from the web editor: keep it
        else:
            im = plain(g['name']['en'])
        save(im, path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
