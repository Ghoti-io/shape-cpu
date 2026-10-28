# Ghoti.io Shape CPU

The CPU rasterizer for the shape family, bit-identical on every host. It is part of the
[Ghoti.io suite](https://github.com/Ghoti-io/suite).

## What is implemented

- `gshcpu_version_string()` and `gshcpu_version_number()`, and the gates every
  library in the shape family runs in `make test`.
- Coverage: 8-bit exact-area coverage of a `shape-geometry` device path under
  the nonzero and even-odd rules, y down, linear alpha. It is the scan converter
  `font` had, moved here without changing a pixel (`font` renders through it).
  `gshcpu_coverage_fill` fills into a rectangle of pixels;
  `gshcpu_coverage_from_path` finds the rectangle and trims. Curves are
  flattened by `shape-geometry`'s flattener first. `_at`, `_total`, `_hash`,
  `_dump`, and limits (`GSHCPU_Limits`) for the extent, the bytes, the cells and
  the points.
- The CPU sink (`render.h`): `gshcpu_sink_create` makes a `shape-scene` sink that
  renders closed lists into RGBA8 and RGBAF32 rasters, straight or
  premultiplied: fills under both rules, clips, opacity groups in their blend
  spaces with every Porter-Duff operator, plus and the fifteen blend modes (W3C's, numbered as COLRv1
  numbers them), linear, radial (the two-circle cone) and sweep gradients in
  their interpolation spaces, the foreground, palette entries
  (a fill whose entry is missing is refused, as the plan says), pictures and
  outline glyph runs, in premultiplied `double` with one rounding at the write.
  It passes shape-scene's sink conformance suite, cancels between list items,
  takes its memory from a caller's workspace when asked, and draws damage
  exactly. Strokes: a centerline and a style, drawn by `shape-geometry`'s one expansion (every join, cap,
  dash, width space and alignment), the expansion cached by resource id and the map's linear part and fractional
  translation, so that a whole-pixel translation of a stroke reuses it (`GSHCPU_Limits.max_cache_bytes`).
  A stroke's coverage is the area of the union of its pieces, computed exactly in integers at the pixels where
  pieces overlap, not the sum of their areas (`GSHCPU_Limits.max_exact_fragments` bounds the pieces of one pixel and
  `max_exact_crossings` the crossings among them, which is what the time of a pixel grows with; no depth of overlap is a limit).
  A stroke of a glyph run is the stroke of the path that run defines (its glyphs' outlines in the run's space),
  united across the run, unhinted, and cached with the notes of the glyphs it could not draw.
  Image paints (masks-images-patterns story I3): an image resource sampled by a fill or a stroke with `NEAREST`, `LINEAR`,
  `CUBIC` (Mitchell-Netravali, clamped) or `AREA` (the exact mean over the pixel's preimage), a window of the image, an edge
  mode per axis (pad, repeat, reflect, none), a filter space of its own, and a tint on an alpha-only image; every format the
  scene stores (gray, RGB and RGBA at 8, 12 and 16 bits and as floats). The filter is chosen once for a fill by whether its
  map stretches every direction. The image is read in place and never copied, and the sink's sampler equals the image oracle's
  vectors; `GSHCPU_Limits.max_area_pixel_work` and `max_image_work` bound the work of `AREA` before any pixel is drawn.
  `gshcpu_render_hash` and `gshcpu_render_dump` spell a render; the
  fixture set's renders are committed goldens, the same on every target.
  `examples/render.c` renders a list; `examples/stroke.c` strokes a path and shows the cache serve a translation;
  `examples/image.c` draws an image with two filters and checks what each does.

## Building

```bash
make test PREFIX="$PWD/.local"
make install PREFIX="$PWD/.local"
```

Dependencies are found by pkg-config and nothing else; a missing one is an
error that names the fix. The libraries this one needs: cutil, color, math,
image, shape-geometry and shape-scene.

Determinism is a requirement, not a hope: every `double` stage builds with
`-ffp-contract=off` and no x87, and elementary functions come from `math`, not
libm. `make check-gates` shows each of those rules failing on a planted
violation.

## License

LGPL-3.0-only. See `COPYING` and `COPYING.LESSER`. Patches are not being
accepted at this time; see `CONTRIBUTING.md`.
