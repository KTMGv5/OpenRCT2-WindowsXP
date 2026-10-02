#!/usr/bin/env python3
"""
Generate Windows XP and modern Windows compliant icon.ico.
Windows XP does NOT support PNG-compressed icons (which were introduced in Windows Vista).
All icons must be uncompressed 32-bit (ARGB) or 8-bit/4-bit DIB bitmaps.
"""

import os
import struct
from PIL import Image

def generate_xp_icon():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    base_256 = os.path.join(script_dir, 'icon_x256.png')
    target_ico = os.path.join(script_dir, 'icon.ico')

    base_im = Image.open(base_256).convert('RGBA')

    # Standard Windows XP & high-DPI sizes
    # Ordered descending so base image is the largest
    all_sizes = [256, 128, 96, 64, 48, 40, 32, 24, 16]
    frames = []

    for sz in all_sizes:
        fn = os.path.join(script_dir, f'icon_x{sz}.png')
        if os.path.exists(fn):
            im_rgba = Image.open(fn).convert('RGBA')
        else:
            im_rgba = base_im.resize((sz, sz), Image.Resampling.LANCZOS)

        # 32-bit ARGB (alpha-blended)
        frames.append(im_rgba)

        # For standard XP sizes (48, 32, 24, 16), also add 8-bit palette
        if sz in (48, 32, 24, 16):
            im_8 = im_rgba.convert('P', palette=Image.Palette.ADAPTIVE, colors=256)
            frames.append(im_8)

    # Save as uncompressed DIB (bitmap_format='bmp')
    frames[0].save(
        target_ico,
        format='ICO',
        sizes=[f.size for f in frames],
        append_images=frames[1:],
        bitmap_format='bmp'
    )

    print(f"Generated {target_ico} (size: {os.path.getsize(target_ico)} bytes)")

    # Verify every frame is uncompressed DIB (PNG=False)
    with open(target_ico, 'rb') as f:
        reserved, type_, count = struct.unpack('<HHH', f.read(6))
        print(f"Verified {count} icons in {os.path.basename(target_ico)}:")
        for i in range(count):
            w, h, colors, res, planes, bpp, size, offset = struct.unpack('<BBBBHHII', f.read(16))
            pos = f.tell()
            f.seek(offset)
            magic = f.read(8)
            f.seek(pos)
            is_png = magic.startswith(b'\x89PNG')
            w = 256 if w == 0 else w
            h = 256 if h == 0 else h
            print(f"  #{i:2d}: {w:3d}x{h:3d}, bpp={bpp:2d}, size={size:6d}, PNG={is_png}")
            assert not is_png, f"Icon #{i} has PNG compression, which breaks Windows XP!"

if __name__ == '__main__':
    generate_xp_icon()
