#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Малює три .ico для NEWIMG/PACK/UNPACK — дискета 3.5" з міткою внизу.

Іконки в репозиторії вже лежать готові; скрипт потрібен, тільки якщо їх
захочеться перемалювати. Розміри 16/32/48 малюються кожен окремо, а не
масштабуванням: на 16 пікселях згладжена копія 48-ї перетворюється на кашу.

    python3 mkicon.py        # перезаписує newimg.ico, pack.ico, unpack.ico
    python3 mkicon.py -p     # ще й preview.png, щоб подивитись очима

Потрібен Pillow.
"""
import os, sys
from PIL import Image, ImageDraw

SIZES = (16, 32, 48)

BODY      = (52, 73, 94, 255)      # корпус дискети
BODY_EDGE = (30, 44, 60, 255)
SHUTTER   = (185, 194, 204, 255)   # металева шторка
SHUT_EDGE = (120, 132, 145, 255)
SLOT      = (90, 100, 112, 255)
LABEL     = (245, 243, 236, 255)   # паперова наліпка
LABEL_LN  = (150, 155, 160, 255)

GREEN  = (60, 160, 70, 255)        # NEWIMG — плюс
BLUE   = (40, 110, 190, 255)       # PACK   — стрілка всередину
ORANGE = (215, 130, 30, 255)       # UNPACK — стрілка назовні
WHITE  = (255, 255, 255, 255)

def r(size, k):
    return int(round(size * k))

def floppy(d, s):
    m = max(1, r(s, 0.0625))
    x1, y1, x2, y2 = m, m, s - 1 - m, s - 1 - m
    bevel = r(s, 0.16) if s >= 32 else 0

    d.rectangle([x1, y1, x2, y2], fill=BODY, outline=BODY_EDGE)
    if bevel:                                   # зрізаний правий верхній ріг
        d.polygon([(x2 - bevel, y1), (x2, y1), (x2, y1 + bevel)],
                  fill=(0, 0, 0, 0))
        d.line([(x2 - bevel, y1), (x2, y1 + bevel)], fill=BODY_EDGE)

    # шторка
    sx1, sx2 = r(s, 0.30), r(s, 0.68)
    sy1, sy2 = y1 + max(1, r(s, 0.04)), r(s, 0.40)
    d.rectangle([sx1, sy1, sx2, sy2], fill=SHUTTER,
                outline=SHUT_EDGE if s >= 32 else None)
    d.rectangle([sx1 + max(1, r(s, 0.03)), sy1 + max(1, r(s, 0.06)),
                 sx1 + r(s, 0.13), sy2 - max(1, r(s, 0.06))], fill=SLOT)

    # наліпка
    lx1, lx2 = r(s, 0.20), r(s, 0.80)
    ly1, ly2 = r(s, 0.54), y2 - max(1, r(s, 0.04))
    d.rectangle([lx1, ly1, lx2, ly2], fill=LABEL)
    if s >= 32:
        for i in range(2 if s < 48 else 3):
            yy = ly1 + r(s, 0.07) + i * max(2, r(s, 0.09))
            if yy < ly2 - 1:
                d.line([(lx1 + 2, yy), (lx2 - 2, yy)], fill=LABEL_LN)

def badge(d, s, color, glyph):
    """Кружечок у правому нижньому кутку: + = новий, стрілки = в образ / з нього."""
    rad = 4 if s < 32 else r(s, 0.21)
    off = 2 if s >= 32 else 1                   # щоб біла обвідка не вилізла за край
    cx = cy = s - off - rad
    if s >= 32:
        d.ellipse([cx - rad - 1, cy - rad - 1, cx + rad + 1, cy + rad + 1], fill=WHITE)
    d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], fill=color)

    if s < 32:                                  # 16 пікселів: малюємо по клітинках
        if glyph == 'plus':
            d.line([(cx - 2, cy), (cx + 2, cy)], fill=WHITE)
            d.line([(cx, cy - 2), (cx, cy + 2)], fill=WHITE)
        elif glyph == 'in':                     # трикутник вістрям униз, по рядках
            d.line([(cx - 2, cy - 1), (cx + 2, cy - 1)], fill=WHITE)
            d.line([(cx - 1, cy), (cx + 1, cy)], fill=WHITE)
            d.point((cx, cy + 1), fill=WHITE)
        else:                                   # той самий трикутник вістрям угору
            d.point((cx, cy - 1), fill=WHITE)
            d.line([(cx - 1, cy), (cx + 1, cy)], fill=WHITE)
            d.line([(cx - 2, cy + 1), (cx + 2, cy + 1)], fill=WHITE)
        return

    if glyph == 'plus':                         # тонкий хрест, а не грудка
        a = max(2, r(s, 0.11))                  # півдовжина
        t = 1 if s < 48 else 1                  # півтовщина: штрих завжди 3 пікселі
        d.rectangle([cx - a, cy - t, cx + a, cy + t], fill=WHITE)
        d.rectangle([cx - t, cy - a, cx + t, cy + a], fill=WHITE)
    else:
        a = max(1, r(s, 0.09))                  # півдовжина стрілки
        t = max(1, r(s, 0.035))                 # півтовщина хвоста
        w = a + t + (1 if s >= 32 else 0)       # півширина голови
        if glyph == 'in':                       # вниз, у дискету
            d.polygon([(cx - w, cy), (cx + w, cy), (cx, cy + a + 1)], fill=WHITE)
            d.rectangle([cx - t, cy - a - 1, cx + t, cy], fill=WHITE)
        else:                                   # вгору, з дискети
            d.polygon([(cx - w, cy), (cx + w, cy), (cx, cy - a - 1)], fill=WHITE)
            d.rectangle([cx - t, cy, cx + t, cy + a + 1], fill=WHITE)

def write_ico(path, frames):
    """Пишемо .ico самі, класичними DIB-кадрами.

    Pillow вміє і сам, але кладе кадри або PNG-стисненими (їх розуміє лише
    Vista і новіші — на цеховій XP значок був би порожній), або DIB, але без
    маски AND, хоча у заголовку її обіцяє. Тут усе як у документації: заголовок
    із подвоєною висотою, потім BGRA знизу вгору, потім однобітна маска."""
    import struct
    dirent, blobs, off = [], [], 6 + 16 * len(frames)
    for im in frames:
        w, h = im.size
        px = im.load()
        xor = bytearray()
        for y in range(h - 1, -1, -1):                  # DIB іде знизу вгору
            for x in range(w):
                pr, pg, pb, pa = px[x, y]
                xor += bytes((pb, pg, pr, pa))
        rowb = ((w + 31) // 32) * 4                     # рядок маски по 4 байти
        mask = bytearray()
        for y in range(h - 1, -1, -1):
            row = bytearray(rowb)
            for x in range(w):
                if px[x, y][3] == 0:                    # прозоро -> біт 1
                    row[x // 8] |= 0x80 >> (x % 8)
            mask += row
        hdr = struct.pack('<IiiHHIIiiII', 40, w, h * 2, 1, 32, 0,
                          len(xor) + len(mask), 0, 0, 0, 0)
        blob = bytes(hdr + xor + mask)
        dirent.append(struct.pack('<BBBBHHII', w & 0xFF, h & 0xFF, 0, 0, 1, 32,
                                  len(blob), off))
        off += len(blob)
        blobs.append(blob)
    with open(path, 'wb') as f:
        f.write(struct.pack('<HHH', 0, 1, len(frames)))
        for e in dirent:
            f.write(e)
        for b in blobs:
            f.write(b)


def make(kind):
    frames = []
    for s in SIZES:
        im = Image.new('RGBA', (s, s), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        floppy(d, s)
        badge(d, s, {'newimg': GREEN, 'pack': BLUE, 'unpack': ORANGE}[kind],
              {'newimg': 'plus', 'pack': 'in', 'unpack': 'out'}[kind])
        frames.append(im)
    return frames

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    want_preview = '-p' in sys.argv
    kinds = ('newimg', 'pack', 'unpack')
    drawn = {}
    for kind in kinds:
        frames = make(kind)
        write_ico(os.path.join(here, kind + '.ico'), frames)
        drawn[kind] = frames
    print('готово:', ', '.join(k + '.ico' for k in kinds))

    if not want_preview:
        return
    Z, pad = 6, 10                              # аркуш: колонка на програму,
    cell = 48 * Z + pad                         # рядок на розмір, збільшено в 6
    sheet = Image.new('RGB', (3 * cell + pad, 3 * cell + pad), (205, 205, 205))
    for c, kind in enumerate(kinds):
        y = pad
        for im in reversed(drawn[kind]):        # 48, 32, 16
            big = im.resize((im.width * Z, im.height * Z), Image.NEAREST)
            sheet.paste(big, (pad + c * cell, y), big)
            y += cell
    sheet.save(os.path.join(here, 'preview.png'))
    print('       preview.png')

main()
