"""Generate the HyperBrowse application icon at multiple resolutions.

Design: "Speed Frame" — a rounded photo/image frame with a diagonal lightning
bolt cutting through it.  The frame uses the app's accent blue palette and the
bolt is bright gold/amber for contrast and energy.

The icon is rendered at 256×256 using anti-aliased vector drawing, then
downsampled to 128, 64, 48, 32, 24, 20, and 16 px variants.  All sizes are
bundled into a single .ico file.
"""

from PIL import Image, ImageDraw, ImageFont
import math
import os

# ───────────────────────── palette ─────────────────────────
# accent blue from the app's theme
FRAME_FILL   = (42, 100, 172)    # deep blue
FRAME_BORDER = (28, 70, 130)     # darker blue border
FRAME_INNER  = (56, 126, 204)    # lighter blue inner area (the "photo")

# lightning bolt
BOLT_FILL    = (255, 200, 40)    # warm gold
BOLT_BORDER  = (200, 140, 10)    # darker gold outline
BOLT_HIGHLIGHT = (255, 235, 140) # bright highlight

# subtle mountain silhouette inside frame
MOUNTAIN     = (36, 85, 148)     # slightly lighter than frame fill

# background
BG           = (0, 0, 0, 0)      # transparent

SIZE = 256  # master render size

def draw_rounded_rect(draw, xy, radius, fill=None, outline=None, width=1):
    """Draw a rounded rectangle."""
    x0, y0, x1, y1 = xy
    r = radius
    # corners
    draw.pieslice([x0, y0, x0 + 2*r, y0 + 2*r], 180, 270, fill=fill, outline=outline, width=width)
    draw.pieslice([x1 - 2*r, y0, x1, y0 + 2*r], 270, 360, fill=fill, outline=outline, width=width)
    draw.pieslice([x0, y1 - 2*r, x0 + 2*r, y1], 90, 180, fill=fill, outline=outline, width=width)
    draw.pieslice([x1 - 2*r, y1 - 2*r, x1, y1], 0, 90, fill=fill, outline=outline, width=width)
    # edges
    draw.rectangle([x0 + r, y0, x1 - r, y1], fill=fill)
    draw.rectangle([x0, y0 + r, x0 + r, y1 - r], fill=fill)
    draw.rectangle([x1 - r, y0 + r, x1, y1 - r], fill=fill)
    if outline:
        draw.line([x0 + r, y0, x1 - r, y0], fill=outline, width=width)
        draw.line([x0 + r, y1, x1 - r, y1], fill=outline, width=width)
        draw.line([x0, y0 + r, x0, y1 - r], fill=outline, width=width)
        draw.line([x1, y0 + r, x1, y1 - r], fill=outline, width=width)


def draw_polygon_aa(img, points, fill, outline=None, outline_width=2):
    """Draw an anti-aliased polygon by rendering at 2x and downsampling."""
    # Use supersampling for better AA
    ss = 2
    w, h = img.size
    big = Image.new("RGBA", (w * ss, h * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    scaled_pts = [(x * ss, y * ss) for x, y in points]
    d.polygon(scaled_pts, fill=fill)
    if outline:
        d.polygon(scaled_pts, outline=outline, width=outline_width * ss)
    big = big.resize((w, h), Image.LANCZOS)
    img.alpha_composite(big)


def generate_icon(output_path):
    # work at 2x for supersampling then downsample
    SS = 2
    sz = SIZE * SS

    img = Image.new("RGBA", (sz, sz), BG)
    draw = ImageDraw.Draw(img)

    margin = int(sz * 0.06)
    frame_r = int(sz * 0.14)

    # ── outer frame (the "photo card") ──
    fx0, fy0 = margin, margin
    fx1, fy1 = sz - margin, sz - margin
    draw_rounded_rect(draw, (fx0, fy0, fx1, fy1), frame_r, fill=FRAME_BORDER)

    border_w = int(sz * 0.02)
    draw_rounded_rect(draw, (fx0 + border_w, fy0 + border_w,
                              fx1 - border_w, fy1 - border_w),
                       frame_r - border_w, fill=FRAME_FILL)

    # ── inner photo area ──
    inset = int(sz * 0.12)
    inner_r = int(sz * 0.08)
    ix0, iy0 = fx0 + inset, fy0 + inset
    ix1, iy1 = fx1 - inset, fy1 - inset
    draw_rounded_rect(draw, (ix0, iy0, ix1, iy1), inner_r, fill=FRAME_INNER)

    # ── mountain silhouette inside photo area ──
    mbase = iy1
    # left mountain
    m1_peak_x = ix0 + (ix1 - ix0) * 0.30
    m1_peak_y = iy0 + (iy1 - iy0) * 0.35
    # right mountain (taller)
    m2_peak_x = ix0 + (ix1 - ix0) * 0.65
    m2_peak_y = iy0 + (iy1 - iy0) * 0.22

    mountain_pts = [
        (ix0, mbase),
        (m1_peak_x - (ix1-ix0)*0.15, mbase),
        (m1_peak_x, m1_peak_y),
        (m1_peak_x + (ix1-ix0)*0.10, mbase - (mbase - m1_peak_y)*0.45),
        (m2_peak_x - (ix1-ix0)*0.12, mbase - (mbase - m2_peak_y)*0.30),
        (m2_peak_x, m2_peak_y),
        (m2_peak_x + (ix1-ix0)*0.22, mbase),
        (ix1, mbase),
    ]
    draw.polygon(mountain_pts, fill=MOUNTAIN)

    # ── small sun circle in photo area ──
    sun_cx = ix0 + (ix1 - ix0) * 0.22
    sun_cy = iy0 + (iy1 - iy0) * 0.22
    sun_r = int((ix1 - ix0) * 0.08)
    draw.ellipse([sun_cx - sun_r, sun_cy - sun_r, sun_cx + sun_r, sun_cy + sun_r],
                 fill=(255, 235, 140, 200))

    # ── lightning bolt overlay ──
    # The bolt runs from upper-right to lower-left diagonally
    cx, cy = sz / 2, sz / 2
    # Define bolt as a polygon – a classic zig-zag lightning bolt shape
    # Bolt positioned from upper-right area to lower-left
    bolt_points = [
        (cx + sz*0.18, cy - sz*0.38),   # top of bolt
        (cx + sz*0.04, cy - sz*0.02),   # first zig left
        (cx + sz*0.16, cy - sz*0.02),   # step right at middle
        (cx - sz*0.14, cy + sz*0.38),   # bottom of bolt
        (cx + sz*0.00, cy + sz*0.02),   # first zag right
        (cx - sz*0.12, cy + sz*0.02),   # step left at middle
    ]

    # Draw bolt with outline for definition
    # First a slightly larger outline version
    draw_polygon_aa(img, bolt_points, fill=BOLT_FILL, outline=BOLT_BORDER, outline_width=int(sz*0.015))

    # Draw a bright highlight stripe down the center of the bolt
    # (a thinner, brighter bolt inset)
    shrink = 0.7
    bolt_center_x = sum(p[0] for p in bolt_points) / len(bolt_points)
    bolt_center_y = sum(p[1] for p in bolt_points) / len(bolt_points)
    highlight_pts = [
        (bolt_center_x + (x - bolt_center_x) * shrink,
         bolt_center_y + (y - bolt_center_y) * shrink)
        for x, y in bolt_points
    ]
    draw_polygon_aa(img, highlight_pts, fill=BOLT_HIGHLIGHT)

    # ── downsample from supersampled size ──
    master = img.resize((SIZE, SIZE), Image.LANCZOS)

    # ── generate all target sizes ──
    sizes = [256, 128, 64, 48, 32, 24, 20, 16]
    frames = []
    for s in sizes:
        frame = master.resize((s, s), Image.LANCZOS)
        frames.append(frame)

    # Save as .ico (Pillow supports multi-size .ico)
    # The first image is the "main" one; append the rest
    frames[0].save(
        output_path,
        format="ICO",
        sizes=[(f.width, f.height) for f in frames],
        append_images=frames[1:],
    )
    print(f"Icon saved to {output_path}")
    print(f"Sizes: {', '.join(str(s) + 'x' + str(s) for s in sizes)}")

    # Also save a 256px PNG for reference
    png_path = output_path.replace(".ico", "_256.png")
    master.save(png_path)
    print(f"Reference PNG saved to {png_path}")


if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    assets_dir = os.path.join(project_dir, "assets")
    os.makedirs(assets_dir, exist_ok=True)
    output = os.path.join(assets_dir, "HyperBrowse.ico")
    generate_icon(output)
