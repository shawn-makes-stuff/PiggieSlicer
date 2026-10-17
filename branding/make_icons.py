"""Build the app's logo assets from the sticker mark.

Usage: python make_icons.py piggie_logo.jpg RESOURCES_ICONS_DIR

Writes:
  PiggieSlicer.svg                  sticker mark (top bar, welcome dialog, message dialogs, splash)
  PiggieSlicer.ico                  Windows exe/window icon, every size rendered from the vector
  PiggieSlicer.icns                 macOS app icon
  PiggieSlicer_128px.png            Linux/macOS window icon
  PiggieSlicer_192px_grayscale.png  error dialog logo
  splashscreen.jpg                  600x540 splash art (the code adds the dark banner on the left)
"""
import os
import sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import clean_logo as cl

ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
ICNS_SIZES = [1024, 512, 256, 128, 64, 32, 16]
SPLASH = (600, 540)   # must stay this size: SplashScreen.cpp uses it as the logical size
SPLASH_MARK = 0.78    # mark height as a fraction of the splash height
SPLASH_BG = (249, 239, 240)  # #F9EFF0: 12% piggie pink over white; the full-colour mark reads on it

src, out = sys.argv[1], sys.argv[2]
sticker = cl.build(src)["piggie_sticker"]
render = lambda s: cl.render(sticker, s)

cl.write_svg(os.path.join(out, "PiggieSlicer.svg"), sticker)

ico = [render(s) for s in reversed(ICO_SIZES)]
ico[0].save(os.path.join(out, "PiggieSlicer.ico"), sizes=[(s, s) for s in reversed(ICO_SIZES)], append_images=ico[1:])

icns = [render(s) for s in ICNS_SIZES]
icns[0].save(os.path.join(out, "PiggieSlicer.icns"), append_images=icns[1:])

render(128).save(os.path.join(out, "PiggieSlicer_128px.png"))

g = render(192)
g = Image.merge("LA", (g.convert("L"), g.getchannel("A"))).convert("RGBA")
g.save(os.path.join(out, "PiggieSlicer_192px_grayscale.png"))

# splash: the mark centred on a pale pink field (the code adds the piggie-black banner on the left)
w, h = SPLASH
splash = Image.new("RGBA", SPLASH, SPLASH_BG + (255,))
s = round(h * SPLASH_MARK)
splash.alpha_composite(render(s), ((w - s) // 2, (h - s) // 2))
splash.convert("RGB").save(os.path.join(out, "splashscreen.jpg"), quality=95, subsampling=0)
print("written to", out)
