#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Generate the XRK application icon.

XRK = LAN remote-control software (Qt6 / C++). The icon concept:
  - Rounded gradient tile (indigo -> cyan)        -> modern, techy
  - A computer monitor                             -> "desktop / screen"
  - A remote cursor arrow (amber accent)           -> "remote control"
  - Broadcast signal arcs on the right             -> "network / LAN connection"

Pipeline:
  1. Build a precise SVG (arcs computed in code).
  2. Render SVG -> 1024 PNG via Microsoft Edge (headless Chromium).
  3. Post-process with PIL: round-corner transparency + multi-size ICO/PNG.

Only stdlib + Pillow are required (Edge is used for SVG rendering).
"""

import math
import os
import subprocess
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.abspath(__file__))
ICON_DIR = os.path.join(ROOT, "icons")
os.makedirs(ICON_DIR, exist_ok=True)

SVG_PATH = os.path.join(ICON_DIR, "xrk.svg")
HTML_PATH = os.path.join(ICON_DIR, "_preview.html")
PNG_PATH = os.path.join(ICON_DIR, "xrk.png")
ICO_PATH = os.path.join(ICON_DIR, "xrk.ico")

SIZE = 1024
TILE_MARGIN = 40
TILE_RX = 200
TILE_RECT = (TILE_MARGIN, TILE_MARGIN, SIZE - TILE_MARGIN, SIZE - TILE_MARGIN)


# --------------------------------------------------------------------------
# SVG construction
# --------------------------------------------------------------------------
def arc_points(cx, cy, r, a0_deg, a1_deg):
    a0 = math.radians(a0_deg)
    a1 = math.radians(a1_deg)
    return (
        (cx + r * math.cos(a0), cy + r * math.sin(a0)),
        (cx + r * math.cos(a1), cy + r * math.sin(a1)),
    )


def fmt(p):
    return f"{p[0]:.1f},{p[1]:.1f}"


def build_svg():
    # Monitor geometry (centered)
    screen_x, screen_y = 277, 250
    screen_w, screen_h = 470, 360
    screen_rx = 44
    screen_cx = screen_x + screen_w / 2
    screen_cy = screen_y + screen_h / 2

    # Cursor (remote pointer), pointing up-left, sitting on the screen
    tip = (470, 372)
    scale = 1.45
    cursor_base = [
        (0, 0), (0, 96), (26, 73), (45, 110),
        (61, 104), (43, 69), (73, 69),
    ]
    cursor_pts = [(tip[0] + x * scale, tip[1] + y * scale) for x, y in cursor_base]

    # Broadcast arcs (right of the monitor)
    bcx, bcy = screen_x + screen_w + 36, screen_cy
    arc_defs = ""
    for i, r in enumerate((74, 124, 174), start=1):
        p0, p1 = arc_points(bcx, bcy, r, -55, 55)
        op = 0.95 - i * 0.12
        arc_defs += (
            f'  <path d="M {fmt(p0)} A {r} {r} 0 0 1 {fmt(p1)}" '
            f'fill="none" stroke="#ffffff" stroke-width="22" '
            f'stroke-linecap="round" opacity="{op:.2f}"/>\n'
        )

    cursor_poly = " ".join(fmt(p) for p in cursor_pts)

    svg = f'''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="{SIZE}" height="{SIZE}" viewBox="0 0 {SIZE} {SIZE}">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#4f46e5"/>
      <stop offset="0.55" stop-color="#2563eb"/>
      <stop offset="1" stop-color="#06b6d4"/>
    </linearGradient>
    <linearGradient id="screen" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#ffffff"/>
      <stop offset="1" stop-color="#e6eef7"/>
    </linearGradient>
    <linearGradient id="desktop" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#eaf3ff"/>
      <stop offset="1" stop-color="#cfe2fb"/>
    </linearGradient>
    <linearGradient id="cursor" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#fbbf24"/>
      <stop offset="1" stop-color="#f59e0b"/>
    </linearGradient>
    <linearGradient id="base" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#aab7c6"/>
      <stop offset="1" stop-color="#64748b"/>
    </linearGradient>
    <clipPath id="tileClip">
      <rect x="{TILE_RECT[0]}" y="{TILE_RECT[1]}" width="{TILE_RECT[2]-TILE_RECT[0]}" height="{TILE_RECT[3]-TILE_RECT[1]}" rx="{TILE_RX}" ry="{TILE_RX}"/>
    </clipPath>
    <filter id="soft" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="10" stdDeviation="14" flood-color="#0b1220" flood-opacity="0.25"/>
    </filter>
  </defs>

  <!-- Tile -->
  <g clip-path="url(#tileClip)">
    <rect x="{TILE_RECT[0]}" y="{TILE_RECT[1]}" width="{TILE_RECT[2]-TILE_RECT[0]}" height="{TILE_RECT[3]-TILE_RECT[1]}" rx="{TILE_RX}" ry="{TILE_RX}" fill="url(#bg)"/>
    <!-- gloss highlight -->
    <ellipse cx="512" cy="150" rx="380" ry="180" fill="#ffffff" opacity="0.12"/>
    <ellipse cx="300" cy="900" rx="300" ry="140" fill="#0b1220" opacity="0.12"/>

    <!-- Monitor -->
    <g filter="url(#soft)">
      <rect x="{screen_x}" y="{screen_y}" width="{screen_w}" height="{screen_h}" rx="{screen_rx}" fill="url(#screen)"/>
      <rect x="{screen_x+18}" y="{screen_y+18}" width="{screen_w-36}" height="{screen_h-36}" rx="{screen_rx-18}" fill="url(#desktop)"/>
      <!-- a couple of faux window bars on the desktop -->
      <rect x="{screen_x+44}" y="{screen_y+44}" width="{screen_w*0.5:.0f}" height="26" rx="13" fill="#ffffff" opacity="0.85"/>
      <rect x="{screen_x+44}" y="{screen_y+86}" width="{screen_w*0.32:.0f}" height="20" rx="10" fill="#ffffff" opacity="0.7"/>
      <!-- stand -->
      <rect x="{screen_cx-36}" y="{screen_y+screen_h-2}" width="72" height="64" fill="#cbd5e1"/>
      <rect x="{screen_cx-100}" y="{screen_y+screen_h+58}" width="200" height="34" rx="17" fill="url(#base)"/>
    </g>

    <!-- Broadcast signal -->
    <circle cx="{bcx}" cy="{bcy}" r="20" fill="#ffffff"/>
{arc_defs}
    <!-- Remote cursor -->
    <polygon points="{cursor_poly}" fill="url(#cursor)" stroke="#ffffff" stroke-width="6" stroke-linejoin="round"/>
  </g>
</svg>
'''
    return svg


# --------------------------------------------------------------------------
# Rendering
# --------------------------------------------------------------------------
def find_edge():
    candidates = [
        r"C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
        r"C:/Program Files/Microsoft/Edge/Application/msedge.exe",
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    return None


def render_with_edge():
    edge = find_edge()
    if not edge:
        print("[render] Microsoft Edge not found; cannot rasterize SVG.", file=sys.stderr)
        return False

    html = (
        '<!doctype html><html><head><meta charset="utf-8">'
        '<style>html,body{margin:0;padding:0;width:1024px;height:1024px;'
        'overflow:hidden;background:transparent}img{display:block}</style>'
        '</head><body><img src="xrk.svg" width="1024" height="1024"></body></html>'
    )
    with open(HTML_PATH, "w", encoding="utf-8") as f:
        f.write(html)

    cmd = [
        edge, "--headless", "--disable-gpu", "--hide-scrollbars",
        "--force-device-scale-factor=1", "--window-size=1024,1024",
        f"--screenshot={PNG_PATH}",
        "file:///" + HTML_PATH.replace("\\", "/"),
    ]
    try:
        subprocess.run(cmd, check=True, capture_output=True, timeout=120)
    except Exception as e:  # noqa
        print(f"[render] Edge failed: {e}", file=sys.stderr)
        return False
    return os.path.exists(PNG_PATH)


# --------------------------------------------------------------------------
# Post-processing
# --------------------------------------------------------------------------
def apply_round_corners(img):
    """Force the tile's rounded corners to be transparent (robust to
    headless-screenshot white background)."""
    a = Image.new("L", img.size, 0)
    d = ImageDraw.Draw(a)
    d.rounded_rectangle(TILE_RECT, radius=TILE_RX, fill=255)
    img = img.convert("RGBA")
    img.putalpha(a)
    return img


def build_assets():
    img = Image.open(PNG_PATH).convert("RGBA")
    img = apply_round_corners(img)

    # High-res PNG
    img.save(PNG_PATH, "PNG")
    print(f"[assets] wrote {PNG_PATH} ({img.size})")

    # Window-icon PNG (256x256) for embedding via Qt resource, keeps binary small
    window_png = os.path.join(ICON_DIR, "xrk_window.png")
    img.resize((256, 256), Image.LANCZOS).save(window_png, "PNG")
    print(f"[assets] wrote {window_png} (256, 256)")

    # Multi-size ICO (largest frame first, then descending)
    sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
    frames = [img.resize((s, s), Image.LANCZOS) for s in sizes]
    frames_desc = list(reversed(frames))
    frames_desc[0].save(
        ICO_PATH, "ICO",
        sizes=[(s, s) for s in reversed(sizes)],
        append_images=frames_desc[1:],
    )
    print(f"[assets] wrote {ICO_PATH} sizes={sizes}")


def main():
    svg = build_svg()
    with open(SVG_PATH, "w", encoding="utf-8") as f:
        f.write(svg)
    print(f"[svg] wrote {SVG_PATH}")

    if not render_with_edge():
        print("[fatal] rendering failed", file=sys.stderr)
        sys.exit(1)

    build_assets()
    try:
        os.remove(HTML_PATH)
    except OSError:
        pass
    print("[done] icon assets generated.")


if __name__ == "__main__":
    main()
