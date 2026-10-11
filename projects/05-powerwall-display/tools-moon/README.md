# Moon photo (tools-moon)

`make_moon_photo.py` turns a full-moon photograph into `src/moon_photo.c` / used by `src/screen_moon.cpp`.

## Source image

- Title: "Moon Phase and Libration, 2020", frame `phase_full.2347` (3240 x 3240 TIFF), a full-moon view
  with north up as seen from Earth, built from Lunar Reconnaissance Orbiter (LROC WAC mosaic, LOLA) data.
- Page: https://svs.gsfc.nasa.gov/4768
- Direct file: https://svs.gsfc.nasa.gov/vis/a000000/a004700/a004768/phase_full.2347.tif
- Credit line: NASA's Scientific Visualization Studio.
- Licence: public domain. NASA SVS (https://svs.gsfc.nasa.gov/help/): "All of our content is in the public
  domain (unless otherwise noted)". Only the music of the animation versions on that page is not PD; the still
  frame has no such restriction.
- The original is not stored in this repo (12 MB). Download it to a temp folder and run the script.

## Regenerate

```
python tools-moon/make_moon_photo.py <path>/phase_full.2347.tif src/moon_photo.c [preview.png]
```

Python 3 + Pillow. The script detects the disc, crops and resizes to 180 x 180 with the disc centred at
(90,90), radius 88 px, applies a gamma of 1.45 and a small contrast/sharpen lift, blacks everything outside the
disc and writes a little-endian RGB565 `lv_image_dsc_t moon_photo` (64,800 bytes, `const`, in flash).

The attribution is shown on the About screen, not on the moon screen.
