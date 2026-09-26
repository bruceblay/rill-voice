#!/usr/bin/env python3
# Copyright (c) 2026 Bruce Blay
# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture actual renderer frames and assemble the README lead image (C++17 required)."""
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs/images'
# Character, seed, elapsed display frames. Every capture is driven by the engine singing.
SHOTS = {'portrait': (5, 13, 150), 'trio': (0, 3, 150), 'rubin': (1, 5, 150),
         'eyes': (2, 7, 150), 'carriage': (3, 9, 150), 'windows': (4, 11, 150)}

def png(path, width, height, pixels):
    def chunk(kind, data):
        return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))
    scanlines = b''.join(b'\0' + pixels[y*width*3:(y+1)*width*3] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!2I5B', width, height, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(scanlines, 9)) + chunk(b'IEND', b''))

OUT.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    binary = temp / 'preview'
    subprocess.run(['c++', '-std=c++17', '-O2', str(ROOT / 'tools/visual_preview.cpp'), '-o', str(binary)], check=True)
    captures = {}
    for name, (character, seed, frames) in SHOTS.items():
        ppm = temp / f'{name}.ppm'
        subprocess.run([str(binary), str(ppm), str(character), str(frames), str(seed)], check=True)
        captures[name] = ppm.read_bytes().split(b'\n', 3)[3]
        assert len(captures[name]) == 240 * 135 * 3
        png(OUT / f'{name}.png', 240, 135, captures[name])
    # 2x native pixels in three columns, with a small consistent gutter. Never interpolate artwork.
    columns, gap = 3, 16
    width, height = columns * 480 + (columns + 1) * gap, 2 * 270 + 3 * gap
    pixels = bytearray(bytes([237, 236, 229]) * width * height)
    for index, name in enumerate(SHOTS):
        row, column = divmod(index, columns)
        x0, y0 = gap + column * (480 + gap), gap + row * (270 + gap)
        data = captures[name]
        for y in range(270):
            for x in range(480):
                source = ((y // 2) * 240 + x // 2) * 3
                target = ((y0 + y) * width + x0 + x) * 3
                pixels[target:target+3] = data[source:source+3]
    png(OUT / 'characters.png', width, height, pixels)
