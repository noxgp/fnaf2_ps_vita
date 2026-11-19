#!/usr/bin/env python3
# Converts the image bank of a Clickteam Fusion Android application.ccn into a Vita-ready texture pack:
# images are pre-scaled to the Vita screen scale, stored in GPU formats, LZ4 compressed.
#   mkpack.py application.ccn images.pak [scale|-] [stripped.ccn]
import sys, struct, zlib, io, multiprocessing as mp
import numpy as np
from PIL import Image
import lz4.block
import imagequant

def read_bank(path):
    d = open(path, 'rb').read()
    off = 16; ib = None
    while off + 8 <= len(d):
        cid, fl, sz = struct.unpack_from('<HHI', d, off)
        if cid == 0x6666: ib = off + 8
        off += 8 + sz
        if cid == 0x7f7f: break
    o = ib; nh, ni = struct.unpack_from('<HH', d, o); o += 4; out = []
    for _ in range(ni):
        h, = struct.unpack_from('<H', d, o); mode, w, hh, xs, ys, xa, ya = struct.unpack_from('<IHHhhhh', d, o + 2); sz, = struct.unpack_from('<I', d, o + 18)
        out.append((h, mode & 255, w, hh, d[o + 22:o + 22 + sz], (w, hh, xs, ys, xa, ya))); o += 22 + sz
    return nh, out, d, ib

def decode(mode, w, h, data):
    if mode == 5:
        return Image.open(io.BytesIO(data)).convert('RGB'), True
    raw = zlib.decompress(data); bpp = 4 if mode == 0 else 2
    stride = len(raw) // h
    a = np.frombuffer(raw, np.uint8)[:stride * h].reshape(h, stride)[:, :w * bpp]
    if mode == 0:
        px = a.reshape(h, w, 4); opaque = bool((px[:, :, 3] == 255).all())
        return Image.fromarray(px.copy(), 'RGBA'), opaque
    v = a.reshape(h, w, 2); v = v[:, :, 0].astype(np.uint32) | (v[:, :, 1].astype(np.uint32) << 8)
    if mode == 4:
        r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31
        return Image.fromarray(np.dstack([r * 255 // 31, g * 255 // 63, b * 255 // 31]).astype(np.uint8), 'RGB'), True
    if mode == 1:
        r = (v >> 12) & 15; g = (v >> 8) & 15; b = (v >> 4) & 15; al = v & 15
        px = np.dstack([r * 17, g * 17, b * 17, al * 17]).astype(np.uint8)
    else:
        r = (v >> 11) & 31; g = (v >> 6) & 31; b = (v >> 1) & 31; al = v & 1
        px = np.dstack([r * 255 // 31, g * 255 // 31, b * 255 // 31, al * 255]).astype(np.uint8)
    return Image.fromarray(px, 'RGBA'), bool((px[:, :, 3] == 255).all())

SCALE = 544.0 / 768.0
PALETTE = True   # 8-bit palette textures (libimagequant): 1 byte per pixel
def convert(item):
    h, mode, w, hh, data, geo = item
    try:
        im, opaque = decode(mode, w, hh, data)
    except Exception as e:
        return h, None
    mask = b''
    if not opaque: # full-resolution 1-bit collision mask (alpha > 0)
        a = np.asarray(im.convert('RGBA'))[:, :, 3] > 0
        mask = lz4.block.compress(np.packbits(a, axis=1, bitorder='little').tobytes(), store_size=False)
    nw = max(1, int(round(w * SCALE))); nh = max(1, int(round(hh * SCALE)))
    if PALETTE:
        im = im.convert('RGBA')
        if (nw, nh) != (w, hh): im = im.convert('RGBa').resize((nw, nh), Image.LANCZOS).convert('RGBA')
        if opaque: im.putalpha(255)
        q = imagequant.quantize_pil_image(im, dithering_level=1.0, max_colors=256, min_quality=0, max_quality=100)
        pal = q.getpalette('RGBA') or []
        pal = bytes(pal[:1024]) + bytes(1024 - min(1024, len(pal)))
        raw = pal + q.tobytes(); fmt = 8
    elif opaque:
        im = im.convert('RGB')
        if (nw, nh) != (w, hh): im = im.resize((nw, nh), Image.LANCZOS)
        a = np.asarray(im).astype(np.uint32)
        v = ((a[:, :, 0] >> 3) << 11) | ((a[:, :, 1] >> 2) << 5) | (a[:, :, 2] >> 3)
        raw = v.astype('<u2').tobytes(); fmt = 4
    else:
        if (nw, nh) != (w, hh):
            im = im.convert('RGBa').resize((nw, nh), Image.LANCZOS).convert('RGBA')
        raw = np.asarray(im.convert('RGBA'), np.uint8).tobytes(); fmt = 0
    comp = lz4.block.compress(raw, mode='high_compression', compression=9, store_size=False)
    return h, (nw, nh, fmt, len(raw), comp, geo, mask)

def main():
    src, dst = sys.argv[1], sys.argv[2]
    global SCALE
    if len(sys.argv) > 3 and sys.argv[3] != '-': SCALE = float(sys.argv[3])
    nh, items, ccn, ib = read_bank(src)
    with mp.Pool() as pool: res = dict(pool.map(convert, items, chunksize=4))
    n = nh + 1
    hdr = bytearray(b'FP2I' + struct.pack('<II', 3, n)); table = bytearray(); blob = bytearray()
    ENT = '<IIHHBBHHhhhhII'; base = len(hdr) + n * struct.calcsize(ENT)
    for h in range(n):
        r = res.get(h)
        if not r: table += struct.pack(ENT, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0); continue
        nw, nhh, fmt, rawlen, comp, geo, mask = r
        off = base + len(blob); blob += comp
        moff = base + len(blob) if mask else 0; blob += mask
        table += struct.pack(ENT, off, len(comp), nw, nhh, fmt, 0, *geo, moff, len(mask))
    # stripped data file: same chunks, empty image bank
    if len(sys.argv) > 4:
        sz, = struct.unpack_from('<I', ccn, ib - 4)
        stripped = ccn[:ib - 4] + struct.pack('<I', 4) + struct.pack('<HH', 0, 0) + ccn[ib + sz:]
        open(sys.argv[4], 'wb').write(stripped)
    open(dst, 'wb').write(bytes(hdr + table + blob))
    raw = sum(r[3] for r in res.values() if r)
    print('images', len(res), 'raw MB %.1f' % (raw / 1e6), 'pack MB %.1f' % (len(blob) / 1e6))

if __name__ == '__main__':
    main()
