#!/usr/bin/env python3
"""Draws Browser 9300's app icon: a shaded 3D globe (oceans, continents, a graticule following the
sphere, a specular highlight, a rim light) with a tilted golden orbit ring passing behind and in
front of it. Original artwork, generated here. Renders 8x supersampled, then downsamples.

Writes aif/icon64.bmp + aif/icon64_mask.bmp (64x50) and aif/icon25.bmp + aif/icon25_mask.bmp
(25x20). The 9300 draws a masked bitmap where the mask is BLACK, so the masks are black on the
icon and white around it. The mask is 1-bit (no alpha), so edge pixels are blended toward the
Desk's light background."""
import math, os, sys
import numpy as np
from PIL import Image

BG = np.array([235, 235, 235], float)          # the Desk behind the icon (edge blending)


def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def land(lon, lat):
    """1 on continents: a few soft blobs on the sphere, plus wobble."""
    v = np.zeros_like(lon)
    blobs = [(-0.75, 0.6, 0.42), (-0.45, -0.25, 0.32), (0.6, 0.35, 0.4), (0.95, -0.45, 0.25),
             (-1.7, 0.25, 0.3), (1.8, 0.05, 0.3)]
    for clon, clat, r in blobs:
        d = np.arccos(np.clip(np.sin(lat) * math.sin(clat) + np.cos(lat) * math.cos(clat) * np.cos(lon - clon), -1, 1))
        v = np.maximum(v, 1 - d / r)
    wob = 0.18 * np.sin(lon * 5 + lat * 3) * np.sin(lat * 7 - lon * 2)
    return smooth(0.02, 0.12, v + wob)


def render(W, H, ss=8):
    small = W < 40                          # 25x20: a wider ring, a smaller highlight
    bw0, bw1 = (0.13, 0.22) if small else (0.05, 0.095)
    w, h = W * ss, H * ss
    ys, xs = np.mgrid[0:h, 0:w].astype(float)
    cx, cy = w / 2, h / 2
    R = min(h * 0.41, w * 0.31)
    nx, ny = (xs - cx) / R, (ys - cy) / R
    r2 = nx * nx + ny * ny
    inside = r2 <= 1
    nz = np.sqrt(np.clip(1 - r2, 0, 1))
    # the globe turned and tilted a little: sphere coordinates of each visible point
    tilt = math.radians(-18)
    yy = ny * math.cos(tilt) - nz * math.sin(tilt)
    zz = ny * math.sin(tilt) + nz * math.cos(tilt)
    lat = -np.arcsin(np.clip(yy, -1, 1))
    lon = np.arctan2(nx, zz) + 0.35
    # light from the upper left, towards the viewer
    L = np.array([-0.55, -0.6, 0.58]); L /= np.linalg.norm(L)
    diff = np.clip(nx * L[0] + ny * L[1] + nz * L[2], 0, 1)
    ocean_hi, ocean_lo = np.array([70, 160, 255.]), np.array([8, 40, 120.])
    base = ocean_lo + (ocean_hi - ocean_lo) * diff[..., None] ** 0.8
    g = land(lon, lat)[..., None]
    land_hi, land_lo = np.array([140, 220, 90.]), np.array([20, 85, 30.])
    landc = land_lo + (land_hi - land_lo) * diff[..., None] ** 0.8
    col = base * (1 - g) + landc * g
    # the graticule: thin light lines every 30 degrees, fading at the limb
    def lines(a, step, width):
        d = np.abs(((a / step) + 0.5) % 1 - 0.5) * step
        return 1 - smooth(width * 0.5, width, d)
    grid = np.maximum(lines(lon, math.radians(30), 0.035), lines(lat, math.radians(30), 0.035)) * (0.25 + 0.75 * nz)
    col = col * (1 - 0.35 * grid[..., None]) + np.array([210, 235, 255.]) * 0.35 * grid[..., None]
    # specular highlight and a soft gloss cap
    H_ = L + np.array([0, 0, 1.]); H_ /= np.linalg.norm(H_)
    spec = np.clip(nx * H_[0] + ny * H_[1] + nz * H_[2], 0, 1) ** (90 if W < 40 else 40)
    gloss = smooth(0.55, 0.0, np.hypot(nx + 0.35, ny + 0.45)) * 0.35
    col = col + (255 - col) * np.clip(spec * 1.1 + gloss, 0, 1)[..., None]
    # rim light on the shadow side (atmosphere)
    rim = smooth(0.75, 1.0, np.sqrt(r2)) * np.clip(nx * 0.6 + ny * 0.5 + 0.5, 0, 1)
    col = col + (np.array([120, 200, 255.]) - col) * (0.6 * rim)[..., None]
    alpha = inside.astype(float)
    # the orbit ring: a tilted ellipse band; its far half is hidden by the globe
    a, b = (R * 1.36, R * 0.48) if small else (R * 1.5, R * 0.40)
    ang = math.radians(-14)
    rx = (xs - cx) * math.cos(ang) + (ys - cy) * math.sin(ang)
    ry = -(xs - cx) * math.sin(ang) + (ys - cy) * math.cos(ang)
    e = np.sqrt((rx / a) ** 2 + (ry / b) ** 2)
    band = 1 - smooth(bw0, bw1, np.abs(e - 1))
    front = ry > 0                          # the lower half of the ellipse passes in front
    show = band * np.where(front | ~inside, 1.0, 0.0)
    shade = np.clip(0.55 + 0.45 * (-ry / b), 0.25, 1.0)        # lit on its upper side
    t = np.clip((e - 1) / bw1 * 0.5 + 0.5, 0, 1)              # across the band: a tube look
    tube = 0.55 + 0.45 * np.sin(t * math.pi)
    gold = np.array([255, 185, 30.]) * (shade * tube)[..., None] + np.array([255, 250, 220.]) * (np.clip(np.sin(t * math.pi) - 0.75, 0, 1) * 3)[..., None]
    gold = np.clip(gold, 0, 255)
    col = col * (1 - show[..., None]) + gold * show[..., None]
    alpha = np.maximum(alpha, show)
    # downsample (box filter): colour weighted by coverage
    def down(arr):
        return arr.reshape(H, ss, W, ss, *arr.shape[2:]).mean(axis=(1, 3))
    cov = down(alpha)
    premul = down(col * alpha[..., None])
    rgb = np.where(cov[..., None] > 0, premul / np.maximum(cov[..., None], 1e-6), BG)
    # a 1-bit mask: half covered or more is drawn; partial pixels blend toward the background
    rgb = rgb * cov[..., None] + BG * (1 - cov[..., None])
    mask = cov >= 0.5
    return np.clip(rgb, 0, 255).astype(np.uint8), mask


def save(W, H, name, outdir):
    rgb, mask = render(W, H)
    img = Image.fromarray(rgb, "RGB")
    img.save(os.path.join(outdir, name + ".bmp"), "BMP")
    m = np.where(mask, 0, 255).astype(np.uint8)          # black = drawn (this phone's convention)
    Image.fromarray(np.stack([m, m, m], -1), "RGB").save(os.path.join(outdir, name + "_mask.bmp"), "BMP")
    return img, mask


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "aif")
    for (W, H, n) in [(64, 50, "icon64"), (25, 20, "icon25")]:
        save(W, H, n, out)
    print("written to", out)
