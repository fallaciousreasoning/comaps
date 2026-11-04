#!/usr/bin/env python3
"""Builds the offline hillshade bundle read by drape_frontend/hillshade_renderer.cpp.

Input is a {z}/{x}/{y}.png pyramid of terrain-RGB tiles. Each output tile is a 256px greyscale
JPEG of signed shading, where 128 is flat ground, darker is shadow and lighter is highlight.
Each tile keeps a GUTTER px border of its neighbours' shading, so filtering is seamless.

The bundle is a sequence of records, lowest zoom first, all integers little-endian:
    [magic "TPYR": u32][z: u8][x: u32][y: u32][size: u32][size bytes of JPEG]

Usage: hillshade_bundle.py <tile_dir> <output_file> [--min-zoom 5] [--max-zoom 11] [--quality 85]
"""
import argparse
import io
import math
import os
import struct
from functools import partial
from multiprocessing import Pool

import numpy as np
from PIL import Image

MAGIC = 0x52595054
TILE = 256
GUTTER = 2
# Heights needed around a tile: the gutter, rescaling slack and the slope kernel.
PADDING = GUTTER + 2
EARTH_CIRCUMFERENCE = 40075016.686
AZIMUTH = math.radians(335)
ALTITUDE = math.radians(45)
# Terrain looks flatter on coarser tiles, so steepen it per level as MapLibre does.
ZOOM_EXAGGERATION = 0.3


def load_heights(src, z, x, y):
    path = f'{src}/{z}/{x}/{y}.png'
    if not os.path.exists(path):
        return None
    rgba = np.asarray(Image.open(path).convert('RGBA'), dtype=np.float64)
    heights = (rgba[..., 0] * 65536 + rgba[..., 1] * 256 + rgba[..., 2]) * 0.1 - 10000
    return np.where(rgba[..., 3] > 0, heights, 0.0)


def load_padded_heights(src, z, x, y):
    """Returns the tile with a PADDING px border taken from its neighbours."""
    out = np.zeros((TILE + 2 * PADDING, TILE + 2 * PADDING))
    # (offset, destination, source) along one axis.
    spans = ((-1, slice(0, PADDING), slice(TILE - PADDING, TILE)),
             (0, slice(PADDING, TILE + PADDING), slice(0, TILE)),
             (1, slice(TILE + PADDING, TILE + 2 * PADDING), slice(0, PADDING)))
    for dy, dst_y, src_y in spans:
        for dx, dst_x, src_x in spans:
            heights = load_heights(src, z, (x + dx) % (1 << z), y + dy)
            if heights is not None:
                out[dst_y, dst_x] = heights[src_y, src_x]
    return out


def add_gutter(shading):
    """Resamples padded shading so the tile fills TILE - 2 * GUTTER px, inside a gutter."""
    pixels = np.arange(TILE) + 0.5 - GUTTER
    # Index into `shading`, whose first pixel is PADDING - 1 px outside the tile.
    position = pixels * TILE / (TILE - 2 * GUTTER) - 0.5 + PADDING - 1
    low = np.floor(position).astype(int)
    weight = position - low
    rows = shading[low] * (1 - weight)[:, None] + shading[low + 1] * weight[:, None]
    return rows[:, low] * (1 - weight) + rows[:, low + 1] * weight


def shade(src, max_zoom, z, x, y):
    h = load_padded_heights(src, z, x, y)
    rows = (y * TILE + np.arange(1 - PADDING, TILE + PADDING - 1) + 0.5) / (TILE << z)
    latitude = np.arctan(np.sinh(math.pi * (1 - 2 * rows)))
    pixel_size = (EARTH_CIRCUMFERENCE / (TILE << z) * np.cos(latitude))[:, None]

    # Horn's method. Rows run south, so dz_dy is positive when the ground rises to the south.
    nw, n, ne = h[:-2, :-2], h[:-2, 1:-1], h[:-2, 2:]
    w, e = h[1:-1, :-2], h[1:-1, 2:]
    sw, s, se = h[2:, :-2], h[2:, 1:-1], h[2:, 2:]
    exaggeration = 2 ** (ZOOM_EXAGGERATION * (max_zoom - z))
    dz_dx = ((ne + 2 * e + se) - (nw + 2 * w + sw)) / (8 * pixel_size) * exaggeration
    dz_dy = ((sw + 2 * s + se) - (nw + 2 * n + ne)) / (8 * pixel_size) * exaggeration

    sun_x = math.sin(AZIMUTH) * math.cos(ALTITUDE)
    sun_y = -math.cos(AZIMUTH) * math.cos(ALTITUDE)
    sun_z = math.sin(ALTITUDE)
    light = (-dz_dx * sun_x - dz_dy * sun_y + sun_z) / np.sqrt(1 + dz_dx ** 2 + dz_dy ** 2)
    return add_gutter(np.clip(light - sun_z, -1, 1))


def encode_tile(src, max_zoom, quality, tile):
    z, x, y = tile
    pixels = np.round(128 + shade(src, max_zoom, z, x, y) * 127).astype(np.uint8)
    out = io.BytesIO()
    Image.fromarray(pixels).save(out, 'JPEG', quality=quality, optimize=True)
    data = out.getvalue()
    return struct.pack('<IBIII', MAGIC, z, x, y, len(data)) + data


def list_tiles(src, z):
    for x in sorted(os.listdir(f'{src}/{z}'), key=int):
        for name in sorted(os.listdir(f'{src}/{z}/{x}')):
            if name.endswith('.png'):
                yield z, int(x), int(name[:-4])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('tile_dir')
    parser.add_argument('output_file')
    parser.add_argument('--min-zoom', type=int, default=5)
    parser.add_argument('--max-zoom', type=int, default=11)
    parser.add_argument('--quality', type=int, default=85)
    args = parser.parse_args()

    tiles = [t for z in range(args.min_zoom, args.max_zoom + 1) for t in list_tiles(args.tile_dir, z)]
    encode = partial(encode_tile, args.tile_dir, args.max_zoom, args.quality)
    with Pool() as pool, open(args.output_file, 'wb') as out:
        for record in pool.imap(encode, tiles, chunksize=16):
            out.write(record)
    print(f'Wrote {len(tiles)} tiles, {os.path.getsize(args.output_file) / 1e6:.1f} MB')


if __name__ == '__main__':
    main()
