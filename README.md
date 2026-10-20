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
  spaces with every Porter-Duff operator and plus, the foreground, pictures and
  outline glyph runs, in premultiplied `double` with one rounding at the write.
  It passes shape-scene's sink conformance suite, cancels between list items,
  takes its memory from a caller's workspace when asked, and draws damage
  exactly. `gshcpu_render_hash` and `gshcpu_render_dump` spell a render; the
  fixture set's renders are committed goldens, the same on every target.
  `examples/render.c` renders a list.

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
