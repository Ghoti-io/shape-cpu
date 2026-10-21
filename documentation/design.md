# Design

**Status:** step 3 of the shape family: the CPU reference sink. The library
computes 8-bit exact-area coverage of a `shape-geometry` device path, the scan
converter that `font` had, moved here as a pure move (story 9 of the first
milestone), and renders `shape-scene` lists through the sink contract (story
13b): it is the definition every other sink is measured against.

The CPU rasterizer for the shape family, bit-identical on every host.

The dependency rules and the determinism gates that bind this library are in
the Makefile's header comment and in `tools/`: `check-fp-contract`,
`check-edges`, `check-libm` and `check-no-x87`, each shown to fail on a planted
violation by `make check-gates`. The plan is in the workspace, not in this
repository.

## Coverage

Device space has its origin at the top-left corner of the top-left pixel, y
down, and a coordinate is an `int32` count of 1/64 pixel. A device path is
flattened by `shape-geometry`'s integer flattener (tolerance 1/16 pixel,
revision 2) into polylines; each subpath is closed (an open one is filled as if
closed). Every edge deposits two signed numbers into each cell it crosses:
`cover`, the signed height of the crossing in 26.6 units, and `area`, that
height times the sum of the crossing's two x positions within the cell. An edge
is cut at each pixel boundary, the crossing at a boundary rounded once (half
away from zero) and shared by the pieces either side, so the walk stays closed.
A sweep along each row accumulates `cover` times 128 from left to right, which
is the winding number times a pixel's area, and the cell's own pixel takes
`cover - area`. The accumulated area is shifted right by `2 * 6 + 1 - 8` (so a
fully covered pixel is 256), then the absolute value is taken (so the floor
comes before it: a negative area rounds away from zero) and folded by the rule:
nonzero saturates at 255; even-odd folds modulo 512 (winding 2 is outside
again), a ramp up to 256 and back, 256 itself being 255.

This is FreeType's `ftgrays` arithmetic with 6 sub-pixel bits instead of 8,
because 26.6 is the family's pixel space.

The cells of a rectangle live in an array merged as they are made (an edge
stays in a cell for several steps), sorted by a counting sort on the row and an
insertion sort within it, and merged again; `qsort` is not used because the
order of equal keys is the platform's.

### Frames, the rectangle and the margin

`gshcpu_coverage_fill` takes the rectangle (left, top, width, height in device
pixels) and requires every point of the flattened path to lie strictly inside
it (`0 <= x - left * 64 < width * 64`, the same for y). The margin is the
caller's: a closing edge on the last pixel boundary of a shape needs a cell of
its own, so the caller grows its box by a pixel on every side, as `font` does.
`gshcpu_coverage_from_path` makes that rectangle itself from the extent of the
flattened path and trims the result to the pixels that have coverage.

The flattener is not symmetric under negation of a coordinate (a midpoint rounds
half up), so a caller that flips an axis flips the path before asking for
coverage. `font` does: it is y-up, device space is y-down.

### The 26.6 range

A coordinate that does not round into 26.6 never reaches this library
(`shape-geometry` refuses it with `ERR_LIMIT`). Inside, a device coordinate is
held as `int64` until it is known to lie in the rectangle, and only then
narrowed; the extent of a path is computed in `int64`, so no coordinate in the
`int32` range wraps. A path whose extent exceeds `GSHCPU_Limits` (or the
ceiling of 2^24 pixels a side, whatever the caller allows) is `ERR_LIMIT`,
refused before anything is allocated. Nothing is clipped or clamped: clipping,
where a front end wants it, is decided in `double` before it asks for a device
path.

### Limits

`GSHCPU_Limits`: `max_dimension` (the working bitmap's width and height, margin
included), `max_bytes`, `max_cells`, `max_points` (the flattened path). 0 is the
default and `SIZE_MAX` no cap. Each is checked at its bound: a quantity equal to
the cap passes, one more is `ERR_LIMIT`, nothing is truncated and the output is
untouched.

### The hash

`gshcpu_coverage_hash` is FNV-1a 64 in `shape-geometry`'s spelling: `u32`
version 1, `u32` kind `COVR`, `u32` width, `u32` height, `i32` left, `i32` top,
`u32` fill rule, then the rows (not the stride). `font`'s `gfnt_coverage_hash`
keeps its own spelling, which is committed in its goldens.

## The sink (render.h)

`gshcpu_sink_create` makes a `GSHSCENE_Sink` (shape-scene's sink.h, S1 to S16).
It draws `RASTER` targets of RGBA, interleaved, 8-bit unsigned normalized or
32-bit float, straight or premultiplied as the raster's format says; every other
target is `ERR_UNSUPPORTED`. Its caps are every item kind and every operator,
`CAN_BLOCK`, `CALLER_MEMORY`, `CLIP_AA`, `BLEND_SPACES`, `READBACK`,
`GLYPHS_FROM_FONT` (story 14a), every gradient kind with `GRADIENT_SPACES`
(color-paint story P2), and cancel between units with a bound of
`max_points + max_cells + max_bytes` in cells, points and pixels. The planning
pass is the contract layer's; the sink decides each unit with the same predicate
(and a run from a font with shape-scene's route), skips what it refused, and adds
only the entries it finds while drawing: an item past one of its resource
limits, and a glyph's own entry.

### S11 Arithmetic (normative)

The full text is in render.h. In short (D7, D8):

- Every layer is premultiplied `double` RGBA per pixel of the region drawn (the
  extent meet the damage). The root layer is the target, read once; a group's
  layer starts at zero in its blend space.
- A fill's paint is converted to its layer's space once per fill (N3, untouched
  when the spaces are equal) and premultiplied. A group's layer is converted per
  pixel at its pop, and not at all when the spaces are equal.
- Coverage is 8-bit. Each fill's coverage `k8` is weighed by the clips in force in
  its group, in the order they were pushed: `m8 = (m8 * c8 + 127) / 255` from
  `m8 = k8`; a group's composite by the clips in force at its push, from 255.
  `m = m8 / 255`; a pixel with `m8 = 0` is not touched.
- `apply(D, S, m, op) = (1 - m) D + m (Fa Cs + Fb Cd)` per component, in that
  order, no fused multiply-add (`PLUS`: `min(1, Cs + Cd)`). A bounded operator
  leaves a pixel where its scaled result is zero untouched.
- Transfers are `math`'s `gmath_pow`; the gamut matrices are computed in
  `double` from N1's chromaticities; nothing comes from libm, no `long double`,
  and `float` only at the float store.
- Saturation (shape-scene's N1, Corey 2026-10-21): each decode, matrix result and
  encode of a conversion, every un-premultiply (a group's, G4's, a straight
  store's) and every `apply` is clamped to `[-2^64, 2^64]`; an un-premultiply
  past the bound is not divided, so no infinity is formed and every value a
  float target receives is finite.
- The one rounding of a render is the write of a touched pixel: to 8 bits,
  `floor(v * 255 + 1/2)` clamped (of `C / A` for a straight target, 0 where `A`
  is 0); to `float`, rounded once. A pixel no unit touched is not written.
- Tolerance: before the write within `1e-12` times `max(1, |v|)` of the
  rational oracle and exact where the case is dyadic with no transfer or matrix;
  after it, the oracle rounded the same way, or the other side within `1e-9` of
  a boundary.

Measured (story 13b's gate, `tests/unit/test_render_vectors.cpp`): the worst
error before the write is 1.22e-15 over shape-scene's 82 composite vectors and
this library's 77 render vectors on four formats (with N1's saturation cases
since, 99 and 81, the worst is unchanged); every exact case is equal bit
for bit, and every stored value is the oracle's rounding.

### Gradients (color-paint story P2)

shape-scene's G1 to G14 are drawn per pixel, normatively as render.h's S11 says:

- **The sample** is the pixel's center, `(x + 1/2, y + 1/2)` in device space, weighted by the pixel's coverage and
  clips as a solid fill's. One sample a pixel is what Canvas, Skia and Blend2D do, and what keeps a damaged render
  equal to the whole render inside the damage.
- **The map.** The chain (instances, item, the gradient's map as the leaf, the target's last) is composed by
  `gshgeom_map_compose_chain` (AD-24: only shape-geometry composes) and the one composed map inverted once a fill:
  the 2x2 part scaled by the power of two of its largest entry (exact), its determinant the exact sum of its two
  products rounded once. A map whose inverse is not finite, and a fill whose rectangle in the whole render maps
  outside half of G14's sampling domain, are this sink's resource limit (S6, S4).
- **The stops** come from `gshscene_gradient_resolve`, once a gradient a render (an open-addressed table keyed by the
  resource id, in the render's block, so caller memory allocates nothing), made interpolants once. The interpolated
  straight color is converted per pixel into the layer's space: N3 is not linear, so it cannot be done once.
  This sink's own N2 and N3 (compose.c) are bit for bit shape-scene's, which is internal to it.
- **The arithmetic.** G7's denominator and G8's `a` are formed exactly (Dekker's product and Shewchuk's expansion,
  no fused multiply-add) and rounded once, so the zero tests the rules make are exact and a nonzero value is never
  divided as zero. G8's roots are `q / a` and `c / q` with `q = b + sign(b) sqrt(b^2 - a c)` (no cancellation).
  G9's angle is `gmath_atan2`, exact on the axes. A `t` past `2^431` (only where G8's `a = 0` sends the root to
  infinity, at the cone's edge) is taken at that end, so no NaN reaches a store and the bits do not depend on a
  target's NaN.
- **What draws nothing** (G10, G11) is skipped by the plan's own predicate, `gshscene_gradient_notice`; where G8
  gives nothing the pixel is not touched.

Measured against shape-scene's `tests/gradient_vectors.h` (the gradient oracle's 1860 points): the worst error of
a color component is 6.11e-16 (linear), 7.77e-16 (radial) and 2.22e-16 (sweep), of `t` 2.05e-16, against a stated
tolerance of `1e-12` (`GSHCPU_RENDER_GRADIENT_TOLERANCE`); all 343 exact cases are equal bit for bit, and every
point the oracle says draws nothing is drawn as nothing. No dithering: the goldens are bit-identical on every
target.

### Why the layers cover the region, not bands of it

D7 names `double` *bands*. The compositor works item by item over layers that
cover the whole region drawn, and accumulates each item's coverage a band of
`GSHCPU_RENDER_BAND_ROWS` rows at a time. Cancel (S7) is why: a token set during
unit `k`'s progress call must leave the target holding exactly the first `k`
units' effect. A compositor that went band by band through the whole list would
have finished every unit in the bands above the one it was in, which is not a
prefix of paint order. So the bands are the coverage's (the cell accumulator is
`(width + 1) x 16` cells of two `int64`), and the layers are the region's: 32
bytes a pixel for the root and for each level of group nesting, a byte a pixel
for each level of clip nesting, for the touched map and for one item's coverage.

That is the memory's size, not the work. Each layer is made ready only over the
rectangle its units have reached so far (its dirty rectangle, widened before a
unit applies to a pixel outside it): a group's layer is cleared there, and the
root's values are read from the target there, once, with its touched map
cleared. A group's pop reads zero outside its dirty rectangle, and the write
visits the root's. The values are the ones a
layer made ready over the whole region would hold, so nothing drawn changes; an
empty list touches none of the block and a small item on a large target its own
pixels (`RenderMemory.ARenderTouchesThePixelsItsUnitsReachNotTheRegion` counts
the bytes of a workspace a render writes).

### Geometry

The maps are composed root to leaf with `gshgeom_map_compose_chain` (the
instances', the item's, a glyph's translation, the target's last, AD-24); the
path is made a device path by `gshgeom_device_path_from_model` and flattened by
`gshgeom_device_path_flatten`. The coverage is story 9's arithmetic restricted to
the rows and columns drawn (src/render/cover.c): rows outside are not visited,
the crossings left of the rectangle telescope into one deposit of cover in a
column before it, and those right of it are dropped. Every crossing of a
boundary is computed from the edge's own start (a row) or the row piece's own
start (a column), so the restriction is exact: a pixel inside it has the
coverage the whole path gives it, which `tests/unit/test_render_parts.cpp`
checks against `gshcpu_coverage_fill` on random paths and windows. That is also
what makes a damaged render equal the whole render inside the damage. An outline
glyph run is one shape: its glyphs' windings add.

**Runs from a font (story 14a, AD-5).** shape-cpu takes the fast tier wherever
shape-scene's `gshscene_glyph_route` returns it (AD-13: this is shape-cpu's fast
tier for glyphs): it asks the face's provider for each glyph's device path at the
route's ppem and snapped scale and the origin's fraction, adds the whole pixel as an
exact integer, and flattens it like any device path. On the general route it asks
for the unhinted outline and makes it a device path under the glyph's map
(`gshscene_glyph_map`), after the map. A run is one shape (its glyphs' windings
add). Each glyph's entry (glyph.h, D7) is reported with its index; a glyph the
provider cannot give, or past the flattener's points or the 26.6 range, is refused
and the rest drawn, and under strict the render stops at the item with the
finished prefix. With caller memory the provider's paths come from the workspace,
measured like any item's geometry (a provider's own caches are its own). AD-5's
gate is `tests/unit/test_glyph_render.cpp`: with hinting off, a run on the fast
tier renders byte for byte what the general route renders under the snapped map,
for every outline glyph of shape-scene's test provider, ppem 8 to 72, a 2048-unit
and a 1000-unit face, two instances and five fractions on each axis (260 scenes of
25 glyphs), and the provider's fast count is the glyph count. The goldens include
two fixtures of runs from a font (`font-fast`, `font-general`), so the
cross-target probe hashes both routes on every target. No glyph is cached across
frames (step 5).

A point must lie within 2^24 pixels of the origin (so that a product of two
coordinate differences fits `int64`); a flattened path is at most `max_points`;
an item makes at most `max_cells` cell deposits. Past any of them, or when
`shape-geometry` refuses the map or the device path, the item is refused while
drawing: a `RESOURCE_LIMIT` entry, the item's block skipped, and under strict the
render stops there with the finished prefix in the target.

The cells are counted over what the whole render draws of the item (its pixels
meet the extent and the reach of its clips there), never only over the damage, so
a damage-limited render refuses exactly the items the whole render refuses (S4,
decided 2026-10-21 after story 16's soak found the two disagreeing under a small
cap). Where a damage leaves part of an item undrawn, a bound from its edges alone
(`gshcpu_render_cells_bound`: per edge, its rows in the rectangle plus the
columns its row pieces can cross) settles an item whose count cannot reach the
cap, and the cells are then counted only where they are drawn; otherwise the
scan converter walks the whole rectangle, counting every crossing, and draws the
part (`gshcpu_render_cover_counted`, which draws what the part alone gives). So
at the default cap an item outside the damage costs its geometry and not a pass
over its cells, and a whole render is unchanged.

### Memory

A render takes one block (the walk, the frames, the layers, the maps, the
touched map, the coverage, the clip coverages, the accumulator) from the sink's
allocator, and the geometry of each item from the same allocator, released
before the next item. With `CALLER_MEMORY` all of it comes from the workspace:
the block first, then an arena (src/render/arena.c) for the geometry, reset at
every item. The exact need is the block plus the most any item's geometry takes
from the arena, which `workspace_bytes` finds by converting every item once
through a measure of the arena (the same rule of blocks and headers, so the
measure is the size that repeats the calls). A `CALLER_MEMORY` render sizes
itself inside its workspace, so a frame that brings its workspace allocates
nothing, even to size itself; a workspace too small for the block or for an item
(or none) gives only a need past it, and the render is `ERR_LIMIT` with nothing
allocated and no need in the summary (S9, decided 2026-10-21: before, the sink
measured the need over its own allocator to report it). The query,
`gshscene_sink_workspace_bytes`, gives the exact need, over the sink's allocator
when the caller's workspace is too small. The need depends on the list and the
region only, never on the workspace's address.

### The hash of a render

`gshcpu_render_hash` is FNV-1a 64 in `shape-geometry`'s spelling: `u32` 1, `u32`
`REND`, `u32` the format, `i64` the extent's four edges, then each pixel's
channels as values (a byte, or a `float`'s bit pattern as `u32`), so that a
big-endian host hashes the same render the same. `tools/gen-render-vectors.py`
restates it.

## Tests and gates

`make test` runs the unit tests and the gates. The known answers in
`tests/known_answers.h` are written by `tools/gen-coverage-vectors.py`, which
shares no code with the library: the definition by integration for axis-aligned
polygons (no edge is walked, no cell accumulated), and a second program of the
algorithm for slanted ones; the generator stops if the two disagree on an
axis-aligned case, and `make check-vectors` fails if the file is not what it
writes. `make check-planted` builds each defect in `tools/check-planted.py` and
requires a test to fail on it (the defects it accepts as invisible, with
reasons, are in its docstring). `suite/tools/xarch/shape.sh` (section 7) and
`suite/tools/xwin/shape.sh` run the cross-target probe on every target.

The sink's tests: shape-scene's conformance suite, installed by shape-scene as
`<ghoti.io/shape-scene/testing/sink_conformance.h>` and run unchanged
(`test_render_conformance.cpp`); the gate of S11 against shape-scene's
`composite_vectors.h` (through the primitives, and through whole renders where a
case's coverage is whole) and against `tests/render_vectors.h`, which
`tools/gen-render-vectors.py` writes from its own restatement of N1 to N10, D7
and D8 in exact rationals (`make check-vectors` checks it); the fixture set of
`tests/render_fixtures.h`, whose renders on all four formats are the goldens in
`tests/corpus/render.hash` and which the cross-target probe prints; damage,
extents, readback, cancel against the render of the prefix, every limit at its
bound and one past it, the exact workspace, frames that allocate nothing,
allocation failure at every call, and the same drawing as a recorded and a
retained list. Gradients (`test_render_gradient.cpp`): the evaluation against
shape-scene's installed `gradient_vectors.h`, every pixel of a covering fill
against the primitives at its center under the composed chain, parameter stops
against the drawing list, what draws nothing, the domain limit with and without
a damage, and the inverse's and the parameter's ends; the four gradient
fixtures are goldens like the rest.
