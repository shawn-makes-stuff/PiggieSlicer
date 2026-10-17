"""Clean piggie_logo.jpg into flat vector logos (piggie pink + piggie black).

Usage: python clean_logo.py piggie_logo.jpg OUT_DIR

Writes, each as .svg (one evenodd path per colour) and 1024 px .png:
  piggie_logo     pink + black on transparency
  piggie_sticker  same on a solid white pig-shaped backing (gaps filled, outline OUTLINE px);
                  this is the default mark everywhere
Both share one tight square viewBox, so they line up and swap 1:1.

1. Upsample (bicubic, after a light blur that irons out JPEG blocks) and label every pixel by
   un-mixing: the palette pair that best explains it as a two-colour blend, then the nearer end.
   This stops grey black/white edge pixels being read as pink.
2. Drop specks, fill pinholes per colour.
3. Sticker backing: white gap pixels that see two different shapes within GAP_L along some line
   (straight bridges, so gaps end flush), holes filled, then grown by OUTLINE.
4. Trace with potrace.
"""
import os
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy import ndimage as ndi
import potrace

SS = 4          # supersample for labelling/tracing
GAP_L = 26      # max gap span to bridge (source px); measured gaps are ~23 px
OUTLINE = 24    # sticker outline width (source px), matches the gap width
MARGIN = 0.03   # empty border around the artwork, fraction of the viewBox
PAL = {"white": (255, 255, 255), "pink": (208, 118, 130), "black": (41, 41, 41)}


def build(src):
    names = list(PAL)
    cols = np.array([PAL[n] for n in names], float)
    img = Image.open(src).convert("RGB").filter(ImageFilter.GaussianBlur(0.8))
    W0, H0 = img.size
    a = np.asarray(img.resize((W0 * SS, H0 * SS), Image.BICUBIC)).astype(float)

    # --- un-mix labelling ---
    best_res = np.full(a.shape[:2], np.inf)
    label = np.zeros(a.shape[:2], np.uint8)
    for i in range(3):
        for j in range(i + 1, 3):
            ci, cj = cols[i], cols[j]
            v = cj - ci
            t = np.clip(((a - ci) @ v) / (v @ v), 0, 1)
            res = np.linalg.norm(a - (ci + t[..., None] * v), axis=2)
            better = res < best_res
            best_res[better] = res[better]
            label[better] = np.where(t[better] < 0.5, i, j)

    # --- specks / pinholes (areas in supersampled px) ---
    min_area = 40 * SS * SS
    def clean(m):
        lab, n = ndi.label(m)
        sizes = ndi.sum(m, lab, range(1, n + 1))
        m = np.isin(lab, 1 + np.flatnonzero(sizes >= min_area))
        hl, hn = ndi.label(~m)
        hs = ndi.sum(~m, hl, range(1, hn + 1))
        return m | np.isin(hl, 1 + np.flatnonzero(hs < min_area))
    pink = clean(label == names.index("pink"))
    black = clean(label == names.index("black"))
    pink &= ~black
    fg = pink | black

    # --- sticker backing ---
    def bridge(m, L):
        # a gap pixel sees two *different* shapes (ear/head, black/pink half) within L along some
        # line; requiring different shapes keeps concave stretches of one outline from filling in
        h, w = m.shape
        comp = np.pad(ndi.label(m)[0], L)
        out = np.zeros_like(m)
        for ang in np.deg2rad(np.arange(0, 180, 3)):
            u = np.array([np.cos(ang), np.sin(ang)])
            dp = np.full(m.shape, L + 1); dm = np.full(m.shape, L + 1)
            lp = np.zeros(m.shape, comp.dtype); lm = np.zeros(m.shape, comp.dtype)
            for k in range(L, 0, -1):
                ox, oy = np.round(u * k).astype(int)
                cp = comp[L + oy:L + oy + h, L + ox:L + ox + w]; hit = cp > 0
                dp[hit] = k; lp[hit] = cp[hit]
                cm = comp[L - oy:L - oy + h, L - ox:L - ox + w]; hit = cm > 0
                dm[hit] = k; lm[hit] = cm[hit]
            out |= ((dp + dm) <= L) & (lp != lm)
        return out | m
    small = fg[::SS, ::SS]
    gaps = ndi.binary_fill_holes(bridge(small, GAP_L)) & ~ndi.binary_dilation(small)
    sil = ndi.binary_fill_holes(fg | ndi.binary_dilation(np.kron(gaps, np.ones((SS, SS), bool)), iterations=SS))
    sil = ndi.gaussian_filter(sil.astype(float), SS) > 0.5
    backing = ndi.distance_transform_edt(~sil) <= OUTLINE * SS  # exact round offset

    # --- one square viewBox (0..1000) around the sticker, shared by both ---
    by, bx = np.nonzero(backing)
    cx, cy = (bx.min() + bx.max()) / 2 / SS, (by.min() + by.max()) / 2 / SS
    half = max(bx.max() - bx.min(), by.max() - by.min()) / 2 / SS / (1 - 2 * MARGIN)
    vb = (cx - half, cy - half, 2 * half)
    to_vb = lambda x, y: ((x / SS - vb[0]) / vb[2] * 1000, (y / SS - vb[1]) / vb[2] * 1000)

    def trace(m):
        # potracer inverts bool input (True = background)
        path = potrace.Bitmap(~m).trace(turdsize=40 * SS, alphamax=1.0, opticurve=True, opttolerance=0.2)
        curves = []
        for c in path:
            segs = []
            for s in c.segments:
                if s.is_corner:
                    segs.append(("L", to_vb(s.c.x, s.c.y), to_vb(s.end_point.x, s.end_point.y)))
                else:
                    segs.append(("C", to_vb(s.c1.x, s.c1.y), to_vb(s.c2.x, s.c2.y), to_vb(s.end_point.x, s.end_point.y)))
            curves.append((to_vb(c.start_point.x, c.start_point.y), segs))
        return curves

    art = [("pink", trace(pink)), ("black", trace(black))]
    return {
        "piggie_logo": art,
        "piggie_sticker": [("white", trace(backing))] + art,
    }


hex_ = lambda n: "#%02X%02X%02X" % PAL[n]


def path_d(curves):
    out = []
    for start, segs in curves:
        out.append("M%.2f,%.2f" % start)
        for s in segs:
            out.append(s[0] + " ".join("%.2f,%.2f" % p for p in s[1:]) if s[0] == "C" else "L%.2f,%.2fL%.2f,%.2f" % (*s[1], *s[2]))
        out.append("Z")
    return "".join(out)


def write_svg(path, layers):
    out = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1000 1000" width="1000" height="1000">']
    for n, p in layers:
        out.append(f'  <path id="{n}" fill="{hex_(n)}" fill-rule="evenodd" d="{path_d(p)}"/>')
    out.append("</svg>")
    open(path, "w", encoding="utf-8").write("\n".join(out) + "\n")


def polys(curves, steps=24):
    res = []
    for start, segs in curves:
        p0 = np.array(start); pts = [p0]
        for s in segs:
            if s[0] == "L":
                pts += [np.array(s[1]), np.array(s[2])]; p0 = np.array(s[2])
            else:
                c1, c2, e = (np.array(q) for q in s[1:])
                for t in np.linspace(0, 1, steps)[1:]:
                    pts.append((1-t)**3*p0 + 3*(1-t)**2*t*c1 + 3*(1-t)*t*t*c2 + t**3*e)
                p0 = e
        res.append(pts)
    return res


def render(layers, size, R=8):
    """Even-odd fill per colour at R x, premultiplied box downsample for anti-aliasing."""
    k = size * R / 1000
    rgba = np.zeros((size * R, size * R, 4), float)
    for n, p in layers:
        m = np.zeros((size * R, size * R), bool)
        for poly in polys(p):
            im = Image.new("1", (size * R, size * R), 0)
            ImageDraw.Draw(im).polygon([(x * k, y * k) for x, y in poly], fill=1)
            m ^= np.asarray(im)
        rgba[m, :3] = PAL[n]; rgba[m, 3] = 255
    al = rgba[..., 3:] / 255
    pm = np.concatenate([rgba[..., :3] * al, rgba[..., 3:]], 2).reshape(size, R, size, R, 4).mean((1, 3))
    rgb = np.where(pm[..., 3:] > 0, pm[..., :3] / np.maximum(pm[..., 3:] / 255, 1e-6), 0)
    return Image.fromarray(np.concatenate([rgb, pm[..., 3:]], 2).round().clip(0, 255).astype(np.uint8), "RGBA")


if __name__ == "__main__":
    src, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    for name, layers in build(src).items():
        write_svg(os.path.join(out_dir, name + ".svg"), layers)
        render(layers, 1024).save(os.path.join(out_dir, name + ".png"))
        print(name, {n: len(p) for n, p in layers})
