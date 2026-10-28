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
violation by `make check-gates`.

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

**What "exact area" means where pieces overlap.** The family's definition is that a shape's coverage is its area inside
the pixel, in 8 bits, computed by this saturating sum of signed areas. The sum is that area for a shape whose pieces do
not overlap inside a pixel. Where they do, it is more than the area by the overlap: it credits the part two pieces share
twice. For a stroke item, whose outline is pieces that overlap by construction, the definition is the area of the union
of the pieces, and the mechanism is the exact step of UC1 to UC8 (below, and `render.h`): the saturating sum remains the
mechanism for fills and for the pixels where the pieces of a stroke do not overlap. This is a clarification of how the
definition applies, not a change of the rule. A fill whose own contours overlap (a figure eight under the nonzero rule, a
variable glyph's overlapping contours, an SVG path that crosses itself) has the same property today: the sum credits its
overlap twice at a pixel it covers in part. The exact step for strokes does not change fills, and extending it to them is a
separate, later decision (it would move every golden of a fill with overlapping contours and the oracles `font` agrees with,
which credit the sum), not taken by it.

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
target is `ERR_UNSUPPORTED`. Its caps are the seven item kinds it draws (the stroke, shape-scene's kind 7, among them: SC1 below) and every operator it composites, each named one by one (the
Porter-Duff operators, `PLUS` and the fifteen blend modes; none of PDF's group fields, which the plan refuses),
`CAN_BLOCK`, `CALLER_MEMORY`, `CLIP_AA`, `BLEND_SPACES`, `READBACK`,
`GLYPHS_FROM_FONT` (story 14a), every gradient kind with `GRADIENT_SPACES`
(color-paint story P2), `STROKE_EXPAND` with no stroke feature native, and cancel between units with a bound in cells,
points and pixels that is `max_points + max_cells + max_bytes` for a fill and, for an aligned stroke,
`3 max_points + max_cells + 2 max_bytes + elements * GSHGEOM_STROKE_MAX_DEPTH`. The planning
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
- **What draws nothing** (G10, G11) is skipped by the plan's own predicate, `gshscene_paint_notice`; where G8
  gives nothing the pixel is not touched.

Measured against shape-scene's `tests/gradient_vectors.h` (the gradient oracle's 1860 points): the worst error of
a color component is 6.11e-16 (linear), 7.77e-16 (radial) and 2.22e-16 (sweep), of `t` 2.05e-16, against a stated
tolerance of `1e-12` (`GSHCPU_RENDER_GRADIENT_TOLERANCE`); all 343 exact cases are equal bit for bit, and every
point the oracle says draws nothing is drawn as nothing. No dithering: the goldens are bit-identical on every
target.

### Images (masks-images-patterns story I3)

An image paint is drawn by `src/render/image.c` (the sampler: decode, edge maps, the point filters, the conversions, the tiers)
and `src/render/image_area.c` (`AREA`), through the painter every fill and stroke shares: `image_begin` in `sink.c` composes the
chain with the paint's map as the leaf, chooses the filter once for the fill, opens the image in place, inverts the map, refuses
what is past the domain or the work limits, resolves the tint, and `painter_span` asks the sampler for each pixel's source.
render.h states the rules (IC1 to IC14); what follows is why they are shaped as they are.

**One definition, hoisted.** The general tier is the definition. FT-1, FT-2 and FT-3 do the same operations in the same order
with the parts that do not depend on the pixel found once: a copy is the texel (its weights are exactly one and zero), a separable
map finds the taps of the coordinate that depends on the pixel's row once for the row, an 8-bit decode is a table of the doubles
the division gives. A gate renders 576 maps, filters, edge modes and formats with the tiers on and off and requires the same
bytes, with each tier counted and forced off by a test. `CUBIC` is not a copy at an integer translation: Mitchell-Netravali with
`B = C = 1/3` weighs a texel's center `1/18, 8/9, 1/18`, so a unit-scale cubic draw blurs.

**`AREA` is exact and its work is bounded by the map.** The preimage of a pixel is one parallelogram, translated, so the cells a
pixel touches have one upper bound for the whole item, a function of the composed map and the window and nothing else. That is
what lets a refusal be decided before any pixel is drawn, and be the same with a damage, with the caller's memory and on every
thread. The bound is of the algorithm's visits: a row of cells at a time, the cells the parallelogram covers entirely summed as
a run (past a padded window as a count of the edge texel, a repeating axis's whole periods as that many sums of the window's row)
and only the cells its boundary crosses clipped. A row that holds one of the parallelogram's vertices is clipped across, because
the cross-section of the parallelogram is no longer a trapezoid there: a run found by the cross-sections at the strip's two edges
would count a cell that the vertex cuts as covered (a defect the second engine found first). The rows themselves are visited
one by one, so a preimage that spans millions of rows costs millions of steps and is refused by the bound; the story's whole-period
sums over the other axis (a tile fully inside the parallelogram counted by the window's mean) are not built, and the bound
says what is.

**A second engine.** The row-and-run algorithm is checked against one that clips every cell of the bounding box, on the oracle's
352 regions and on 3000 random parallelograms (60000 once, to see the bound hold) across every edge mode on each axis, windows
and formats. The bound is asserted to be a bound (its visits never pass it) on the same random cases.

**What is kept.** Nothing across renders: the image is read in place, the decode table (2 KiB) and the table of 32 rows' sums
live in the painter's frame, and a hit in the row-sum table is the same doubles as a miss, with the work counted the same either
way. The story's cache of window means, held across renders under `max_cache_bytes`, is not built: it would save the pass over a
row that the per-fill table already saves within a render, at the cost of an eviction rule and a key.

**Layers.** A list that holds an image paint is composited in double layers, not the byte layers the fast fill coverage uses on
an 8-bit target: an image's value is any filtered texel taken to the layer's space, which a float texel or a gamut conversion
takes out of `[0, 1]`, and the byte compositor has one source color a fill. (Found by a fixture that crashed the fast probe.)

**Measured.** The oracle's 1906 point vectors: the sink's sampler is within 0.12 of the tolerance each states at worst, and 624
of 624 exact vectors are bit for bit. Its 352 regions: within 0.001 of the tolerance, 83 of 83 exact bit for bit.

### Placeholders (color-paint story P5)

Nothing draws a placeholder (shape-scene's list.h, PH1) and the planning pass reports it (PH3), so the sink has no code
for it but to go through the unit: it claims no `item_kinds` bit for it, the plan writes the entry whatever the caps
claim, a refused one under strict fails before the sink is called, and an approximated one is gone through as the item
that draws nothing. It adds no cell, point or pixel to the work a unit counts, no scratch memory to size and no static
bytes. The conformance suite's placeholder cases run on this sink and on the test sink's configurations, and
`tests/unit/test_render_placeholder.cpp` holds the pixels and the exact scratch need.

### The plane (color-paint story P4)

A fill of shape-scene's plane (PL1 to PL10) covers every pixel the clips in force allow. There is no outline to
rasterize, so the coverage is 255 over the region the render draws (the extent met with the damage, met with the clips'
rectangles) and the pixel's weight is the clips' alone (D8), which is what a fill of the clips' own region gives pixel
for pixel: an antialiased clip edge is weighed once. The paint is the fill's own tail, a solid or a gradient sampled at
the pixel centers under the same chain, so a gradient plane under an item map moves only the paint. No cell, point or
fragment is charged to the limits; the work is the pixels of the region, which `max_dimension` bounds. The conformance
suite's plane case runs on this sink and on the test sink's configurations, and `tests/unit/test_render_plane.cpp`
holds the pixel equalities with the filled region.

### Blend modes (color-paint story P3)

A group of a blend mode composites at its pop by `apply` with shape-scene's `op_X` (B2), in the enclosing layer's
space (B5): both colors un-premultiplied by N1's evaluation, the mixing function evaluated as B7 and B8 write it in the
order `render.h` states (S11), saturated (B9), weighted, applied, with one rounding at the write. Dodge's and burn's
quotients are never formed past `2^66`. ClipColor forms `L - n`, `x - L` and `C - L` from the differences `C - n`
(B10), and takes a step only when its denominator is positive: written literally, `Lum(C) - n` in doubles is 0 a
rounding off the ray `C = L` (the fuzzer found the NaN of that `0 / 0`), while
`Lum(C - n)` is a sum of terms that are not negative, positive whenever the components are not all equal, and
accurate there; an all-equal triple, B10's `0 / 0` form, has every difference 0 and is left alone. shape-scene's
`kNearRay` holds 24 such samples (the grid refuses them, the ray being a discontinuity). `Lum`'s constants are the doubles nearest `0.3, 0.59, 0.11`. The caps were narrowed to name the operators `apply`
knows before shape-scene's enum grew, and `apply` has no silent default, so an operator it does not know is refused by
the plan and could not be drawn as `CLEAR`.

Measured against shape-scene's `kMix` (1863 samples of the mixing function, 897 exact) and its 417 blend cases: every
exact sample equal bit for bit (a zero's sign aside: the values are reals), and the worst relative error of the
others 4.4e-16 (polynomial modes, `min`, `max`), 1.3e-16 (dodge and burn), 7.1e-15 (soft-light's root) and 1.4e-15
(the non-separable modes), against the `1e-12` of D7. A sample whose evaluation in this order cannot meet the
tolerance (`2^64 + 1 - 2^64` in screen, a value within its own error of one of B10's jumps) is not in the table: the
generator refuses it. Three fixtures (`blend-separable`, `blend-non-separable`, `blend-out-of-range`) add twelve
goldens; every earlier golden is unchanged.

### Strokes (SC1 to SC11)

A stroke is a fill with another coverage, and `render.h` states the rules SC1 to SC11 that make it one. This section
says why they are what they are and what each costs.

**Nothing here strokes.** `src/render/stroke.c` calls `gshgeom_stroke_expand` and covers what it returns; it has no
join, no cap, no offset and no dash. The style reaches it through shape-scene's one conversion
(`gshscene_resource_stroke_geometry`), so the table of fields is written once and a field added to the style cannot be
dropped by one sink and kept by another. Every field is honored by the expansion except the alignment, whose three
parts the sink composes (SC5); that is why the caps claim `STROKE_EXPAND` and no native feature, and why no stroke is
ever refused for its style.

**The translation is split (SC3).** The expansion rounds each vertex to 26.6 after the map, so its bits depend on the
map's translation, and a cache that served one translation's outline for another would make a picture depend on what was
drawn before it. The region does not depend on position, so `gshgeom_stroke_canonical_map` splits the translation into
whole pixels and a fraction in `[0, 1)`; the expansion is made with the fraction only and covered over the rectangle
moved the other way by the whole pixels. The outline is then a function of the centerline, the style, the linear part and
the fraction, and the same bits at every whole-pixel translation, which is the entry a cache can share. A sub-pixel
animation misses every frame; scrolling that snaps to whole pixels hits every frame.

**Three coverages become one (SC5).** The body, the aligned part and the clip path are each covered as a fill is. The
composition is the union formula in D8's integer arithmetic, so `k` is exact where either part is (a centered stroke is
the body's coverage bit for bit) and never above 255. Absent parts are zero, which is also why the arrays for the aligned
part and the clip exist only when the list holds a stroke that aligns.

**What the coverage is, and its tolerance.** The outline is a set of pieces (a quadrilateral for each stretch of a curve,
a triangle or sector for each join, a shape for each cap, a piece for each dash) wound positively, and `k` is the coverage
of what they cover: the area, in the pixel, of their nonzero fill (UC1). That is not what AD-12's accumulation, a
saturating sum of signed areas, gives where two pieces cover a pixel in part and overlap inside it: the sum credits the
overlap twice, up to the overlap more than the union's area (the inner side of a curve or a join, the caps of abutting
dashes). So a pixel that holds two or more pieces of a part is made exact (below), and the sum remains the mechanism for
everything else: for fills (a self-overlapping fill has the property today; extending the exact step to fills is a
separate decision) and for the cells where pieces do not overlap. Interior and exterior pixels are exact. The bracket of
SC5 (`tests/stroke_gate.h`, one function another backend can call on its own coverage) is the stroke's stated tolerance
against the geometric oracle, and `o`, its allowance for what is left of the overlap, is one level (UC12). Before the exact
step it was 64/255, a stopgap: the worst overshoot measured on the build without the step is 61.8/255 (dashes with square
caps over two subpaths), then 49.8 (a closed bevel under a device width), 45.8 to 46.6 (a sharp corner under a non-uniform
map; square caps), 37.5 (the aligned part) and about 20 to 26 at the inner side of an ordinary curve or polygon, and the gate
runs that build (`GSHCPU_RENDER_TIER_EXACT` off) as the control it must fail. The bracket's `D(tau)` is a sixteenth of a
pixel, which hides an overlap error of a few levels on a thin feature, so a second instrument, the union gate, holds every
pixel to a level and a half of an independent scanline sweep of the union of the pieces.

**The exact step (UC1 to UC7, UC13 to UC16).** The dense accumulation is an exact integer sum of the winding over the pixel. The
area of what the nonzero fill covers differs from it by `2 O - 4 N` in doubled units: `O` what is covered by more than one
winding (`(|w| - 1)^+`) and `N` what is wound the wrong way (the 26.6 lattice can turn a sliver of a piece inside out; the fill
covers it, the sum subtracts it). That difference is also the dense value less twice the area where the winding is not zero, and
the step computes that area, so a cell without a second piece is the dense value to the bit: fills, cells of one piece, corners
and the interior never change, and no weight grows with the depth of the winding. The step works in one cell, from its pieces
and the cover to its left. A piece is what the rasterizer deposits for an edge in the cell, and a horizontal one that touches
the cell's left side: the dense pass deposits nothing for it, and it steps the winding along that side. The winding at the
side's top comes from the dense sum: `A_left`, what the cells to the left accumulated, is the integral of the winding along
the side, and the winding steps only where a contour crosses the line, which the cell's own pieces show, so `64 W_top = A_left
- sum J(h) (64 - h)` is one exact integer division whose remainder is a consistency check on every cell (a top or bottom side
would be crossed by the pieces the dense pass keeps, but then the slabs would run in `x`, where the horizontal pieces are the
ones that cross and are not kept). The second check is exact too: the pieces' own cover and area, summed as the rasterizer
summed them, are the cell's dense value as integers, so a piece missed or invented is found. A cell that fails is refused,
never guessed. The orientation sign `eps` is -1 (positive winding goes from +x toward +y, so the inside's left edge goes up)
and a test pins it.

The pieces of a cell are sorted by segment (so that the result does not depend on the order they were found in), a piece and
its reverse cancel (the seam two pieces of an outline share is not a piece of the cell), and the copies left of one segment
are one fragment of a multiplicity: a winding is a sum over edges, and coincident edges enclose no width. Slabs in `y`
between the integer heights of the ends and steps have fragments that each span them, so within a slab their left to right
order is the order of `x`, and two of them meet at most once. The area is not summed gap by gap at every meeting, which is
cubic in a dense cell, but at the meetings. Summing a row's covered widths by parts over the fragments, `sum_gaps Omega_k
(x_{k+1} - x_k) = 64 Omega_n + sum_f x_f (Omega_{k-1} - Omega_k)`, where `Omega` is the weight of a gap's winding (twice "the
winding is not zero" for a part's own overlap, the predicate of UC8's region for the regions) and a fragment's coefficient
`c_f = Omega_left - Omega_right` is constant between the meetings that move it. Integrated over the slab, with `F_f(t)` the
exact antiderivative of `x_f`, that is `64 H Omega_n` plus `d (F_p - F_q)` at each meeting of `p` and `q` (`d` the second
difference of `Omega` there: only the gap between the two changes) plus `c_f F_f(H)` for each fragment at the slab's end. The
order is carried from slab to slab, repaired where a pair met exactly at the boundary, and kept by exchanging neighbours at
their meetings, earliest first, from a binary heap indexed by the pair's place; equal depths in any order, since the
exchanges telescope. A slab with no meeting builds no heap. The cost of a slab is `O(n)` and of a cell `O(64 n' + K log n')`
for `K <= n' (n' - 1) / 2` meetings: the work counted is `m + S (6 n' + 8) + K (ceil(log2 n') + 12)` (and `n'
(ceil(log2 n') + 1)` more for a slab when the meetings are counted first).

The arithmetic is integer, with no depth rounded. A meeting is at the exact depth `N / D` (`D <= 2^13`, `N < 2^19`), and `F`
there is `Num / Den` with `Num = 2 a N D + dx N^2 < 2^46` and `Den = 2 dy D^2 <= 2^33`, taken to `2^-30` as `(Q << 30) + ((Rm <<
30) / Den)` (`Rm < Den`, so `Rm << 30 < 2^63`): exactly `floor(2^30 F)`. The sum is a `uint64_t` that wraps by definition and
whose true value is below `2^13` times `2^30`. Each `F` is low by less than one unit of `2^-30`, a meeting adds two of them times
a second difference of at most `4`, so the area is within `8 K + 2 E` such units of the exact one. The earlier arithmetic, a
boundary rounded to sixteen bits and a floor in every gap of every sub-slab, was within a bound that grew with the square of
the pieces of a cell (about a level at 512 pieces) and so could not be run at a larger cap; this one has the error of a
hundred thousand meetings at under a thousandth of a doubled unit. The area is rounded to a doubled unit with ties up, as before,
with an offset of that bound so that a cell whose exact area is a tie (lattice geometry makes them common: a thin feature of
whole units is a half unit of area) rounds up as it exactly does and not either way by the sign of the error; the earlier
step rounded a tie by the sign of its own error, which is why a few cells of the committed fixtures differ from it by a doubled
unit and none by a byte. The step is held to an engine that shares none of its working (an area from every pairwise meeting
in `long double`, the meetings from testing every pair), to the earlier step kept in the tests (the dual render), and to its
own invariants (each slab's order sorted at its end, the meetings made equal to the meetings counted) in a test build.

**The fast stroke coverage.** `GSHCPU_Limits.stroke_coverage` is `GSHCPU_STROKE_COVERAGE_EXACT` (0) by default, the exact union
of the paragraphs above, or `GSHCPU_STROKE_COVERAGE_FAST` (1): the coverage of a solid stroke's outline summed in integers, the
area and cover each edge leaves in the cells it crosses (the form of font-rs and stb_truetype), kept by rows of sparse cells that
are sorted and swept between, so that the work follows the outline and never the rectangle it lies in, and the magnitude held at
one, which is how Skia and Blend2D draw a stroke whose pieces overlap. It differs from the exact coverage only where pieces of one
stroke overlap (and by at most three levels of 255 elsewhere, the rounding of a float for the floor of an integer): on the
benchmark's stroke scene the two pictures differ by a mean of 0.056 of 255 a byte, 1,769 bytes of 8.3 million by more than 8,
at most 61. It is taken by every stroke the sparse composite takes (a single part, a solid paint, its bound of deposits within
the cap) and by no other; the others, and every fill, use the exact one. It costs about 15 % less time on the scene of 2,500
strokes and a third less on its dashed one.

A render that holds only strokes, on `GSHCPU_FORMAT_RGBA8_PREMULTIPLIED`, with the fast coverage, source-over, solid paints and
no clip, composes straight into the 8-bit target and never forms the double layer, whose memory traffic was most of the time:
each run is blended in four 16-bit lanes of a word, rounding at each step as the 8-bit engines do, an opaque source at full
coverage is a copy, and a render with anything else keeps the double layer. On the benchmark's stroke scene a frame takes
132 ms against 267 ms for the exact coverage, and its dashed scene 43 ms against 101 ms; against the pictures of Blend2D and
Skia the two tiers differ from them by the same mean (0.7 and 0.9 of 255 against Blend2D, 1.9 against Skia).

**The fast fill coverage.** `GSHCPU_Limits.fill_coverage` is `GSHCPU_FILL_COVERAGE_EXACT` (0) by default, or
`GSHCPU_FILL_COVERAGE_FAST` (1): the coverage of a fill, a clip, and a stroke painted with a gradient made by the same lean cover
as a fast stroke's, every path of an item at once, with the magnitude held at one (nonzero) or folded (even-odd). It is taken by
every item whose bound of deposits is within the cap and by no other. On the benchmark's two showcase pictures (2400 by 1500,
glyph outlines, gradients, clips and groups, one thread) it takes a frame from 2.9 s to 0.6 s (typography) and from 1.6 s to
0.6 s (geometry) together with the fast stroke coverage, and the pictures differ from the exact ones by a mean of 0.01 and
0.07 of 255 a byte, where the references differ from them by 1.0 to 1.2.

The same limit lets a gradient fill take its color from a table of the line: 1,025 points of the extent, each converted to the
layer's space and premultiplied once, and kept (two tables, by gradient, paint alpha and space) for the fills that follow. A pixel
costs its parameter and one lookup; the color it gets is that of the nearest point, which differs from the evaluation by the
change of the line over a 1,024th of its extent. A fill under 2,048 pixels that finds no table evaluates the line as before.

On a target of 8-bit pixels, premultiplied or straight, the same limit also holds the layers as bytes: a premultiplied target is the
root itself, drawn in place; a straight one is loaded into a root of premultiplied bytes as the units reach it and stored at the
end, the pixels whose bytes changed (one the drawing left as it loaded is left as it was). A group in the target's space is a byte
layer; a group in another space keeps doubles (its pixels are converted where it is composed, linear light into the target's
through a table). A fill composes in integers with the arithmetic of the fast stroke's compositor, a gradient from its table of
bytes, a group's source-over a row at a time, and a separable blend mode a row at a time in single-precision floats from the
premultiplied channels. The offset on the line of a gradient padded at both ends is found in single precision, eight pixels at a
time where the processor has AVX2 and by the same operations, one at a time, where it has not, so that both give the same bytes. A gradient's
constants for those rows are made once when it is prepared, and the last pixels of a span (a gradient stroke's spans are mostly two
to eight pixels) are one more block of eight lanes, gathered, weighed and stored under a mask that keeps it inside the span. Every operation rounds to a byte, where the exact coverage rounds once at the end, so a
picture differs from the exact one by a fraction of a level on the whole (0.4 to 0.7 of 255 a byte on the two showcase
pictures). A list with a color component outside `[0, 1]` is drawn with doubles, for a byte cannot hold it. The two pictures take
0.08 and 0.15 s a frame on one thread.

The fast coverage is an integer area and cover per cell, in 8192ths of a pixel, with the blocks of sixteen cells (a line of the
sums) that a row has touched kept in a bit each and the sweep taking the cells that hold a sum from those blocks, in order, in
bands of up to 1,024 rows. With a fast coverage the accumulator is given room for a band as tall as the window (within 16 MiB), so that the edges are walked once and not once for each band of the few rows a smaller one holds; a render clears only the first bytes of it that a cover uses, and keeps how many of them are known to be zero. The points are already 64ths of a pixel, so an edge steps the quotient and remainder of a
row's share and its x at a row boundary is the nearest 64th of the line's, found from the edge's origin, so the cover does not
depend on where a band or a stripe begins; only the shares of a piece that spans four cells or more are made from a double. The
compositor blends four pixels at a time with SSE2 where the target has it (the same rounding as the scalar word), and eight with
AVX2 where the processor has it, found when the first run is blended. The frame of 2,500 strokes takes about 50 ms on one thread.

A sink created with `gshcpu_sink_create_with_executor` draws those strokes over 32-row stripes on threads the caller owns.
The library starts none: it asks the executor to start a count of tasks, which return at once, and later to wait for them. The
calling thread queues the next batch of strokes into a second buffer while the tasks draw the first, and takes a share of the
stripes itself when it must wait; a task the executor did not start is drawn by the calling thread. A stripe is drawn by
one task, its strokes in order, and a batch is waited for before the next is started, so the picture is byte-identical for any
count. The strokes are queued with their points copied and handed over when the batch fills, before any stroke that is not
queued, and at the end of the render. A sink with `CALLER_MEMORY` does not queue, since each task needs memory of its own. A batch holds up to 1,024 strokes, so that the start and the wait
of the threads are paid ten times a frame and not forty. At 1, 2, 4 and 8 threads a frame takes 64, 39, 21 and 16 ms, the dashed scene 20, 14,
7 and 6 ms; what remains serial is the opening of each stroke, about 2 ms, and the tail of each batch.

**What a pixel may cost (UC9, UC17, UC19, UC20).** Two limits bound the step, both decided over what the whole render draws of
an item, so a damaged render refuses what the whole one does. `max_exact_fragments` (default 32768, at least twice the densest pixel measured) bounds the raw pieces of a
part in a pixel: the pieces are what the rasterizer finds, and the step's memory and its first pass are linear in them. The
densest pixel measured over every drawing the suite keeps holds 13363 (a line of DejaVu Sans stroked 22 units wide with round
joins and caps, three words), the benchmark's stroke scenes 4294 and the gallery 2359. `max_exact_crossings` (default `2^20`)
bounds the crossings of a pixel, which is what its time grows with and which the pieces do not bound (two pieces cross once, so
`n` fragments make up to `n (n - 1) / 2`): the most any pixel measured makes is 333496, and the time of a pixel built to exactly
`2^20` crossings, the densest cell the lattice allows at the default (a chord grid of 2098 segments), is measured by the dense
tests, which print it. The count is known without sweeping: two fragments meet inside a slab iff their order at its top
differs strictly from their order at its end, so the crossings of a slab are the inversions between the two, a merge count in
`O(n log n)` that also gives the order the next slab starts from. It runs only for a pixel that can pass the budget
(`n (n - 1) / 2` above it, which is a pixel of over 1449 fragments at the default) and stops when it has, so a refused pixel costs
its count and not the budget, and the sweep then makes exactly that many exchanges (a test build asserts it). The pixels of the
walk outside the drawn rectangle whose pieces could pass the budget are collected as the drawn ones are and counted, an aligned
stroke's three parts together, so the refusal is the same however the render is damaged and never depends on the cache. Neither
limit is "no cap" at `UINT64_MAX`; a host that draws outlines it did not author lowers them, which is what they are for. No
winding is a limit (the weight is whether the winding is zero, not how deep it is), where the earlier step refused one past 255
and a line of stroked text reaches 494.

**Rounding at exact ties (UC21).** The exact step rounds the exact area of a pixel to the nearest coverage level, and an area
exactly half way rounds up. The lattice makes such ties common (a feature of whole units is half a unit of area), so a rule
is needed and this is it. The step the sweep replaced rounded a tie by the sign of its own error, which depended on how its
roundings fell and was not the same for equal inputs; it is kept in the tests as the second engine of the dual render, which
allows one level of difference at an exact tie and none anywhere else.

**Parts that share a pixel (UC8).** SC5's composition, `k_b + k_a c' - k_b k_a c'`, is exact where at most one of the three
parts has an edge in the pixel, because the others are then 0 or 1. It is not elsewhere, and S4's bracket could not show it
(its width, a sixteenth of a pixel, is wider than the error): for a thin aligned stroke the band and the fill's edge lie in
every pixel along the path, and a band `[0.1, 0.5]` against a fill `x < 0.3` is 0.2 of the pixel by area and 0.12 by the
formula, twenty levels low. So an aligned stroke is covered by its three parts together, a band of rows at a time, each part
with its own accumulator and its own pieces, and a pixel where two of them hold a piece is the area of `{w_b != 0} U ({w_a
!= 0} & F')` by the same walk with three windings (the clip's `F'` by its rule, or its complement). A pixel where one part
alone has pieces keeps the formula with constants, each part made exact for its own overlap first: the body and the aligned
part as fills of nonzero pieces, the clip by its rule (a star's fill region overlaps itself, and its area is what the
formula's constants stand for; an even-odd clip needs the parity, not the overlap). That is the clip part of a stroke, not a
fill item, and no fill changes.

**Why the cut is canonical (UC2).** An interior cut point is the rounding of a cut from the edge's own start. The two
pieces that share a segment traverse it in opposite directions, so cut from their own ends they get cut points up to a unit
apart, and the sliver between them is a hole in the union: a pixel in the middle of a stroke came out three levels short in
the first measurement of the exact step, where the sum's overlap had hidden it. An edge that goes up is therefore cut from its
upper end, and deposited in its own direction. This changes which lattice points a stroke's pixels are the area of, and no
fill's.

**The fold.** The accumulation of a stroke is negative (its pieces are wound so), and the shared fold shifts before it
drops the sign, so a stroke part would come out a level high wherever the area is not a whole level; its even-odd ramp back
down (512 less the shifted value) rounds the other way again, so the clip of an aligned stroke under even-odd would be high
wherever the odd winding is the larger. The stroke's parts fold the magnitude, reduced by the rule before the shift: the
floor of a level either way round.

**Windows and memory.** The pieces of the exact cells of a band go in a pool by a counting sort (a count per cell from the
first pass, a second pass over the edges storing them); a band whose exact cells hold more than the pool is made exact a
window of cells at a time. The pool, the counts and the one cell's scratch are in the render's block, sized by the limits
(`max_exact_fragments`) and the region alone, so the exact workspace is still exact and the step allocates nothing under
`CALLER_MEMORY`.

**The cache (SC7)** is keyed by resource ids, never by the resources' addresses or contents, because ids are never reused
(a stale entry costs memory and nothing else). The value is the immutable outline. The bound is on the bytes the table
accounts for (an entry's points and subpaths and a fixed overhead for its parts), and the order of eviction is least
recently used first among the entries no render has pinned. A pinned entry that is evicted is unlinked and freed by the
last render to release it. The lock is held for the lookup, the insert and the eviction, never across an expansion, so
two renders that miss the same key both expand and the second to insert drops its copy. A hit checks the entry's parts
against the current `max_points`, so a hit refuses exactly what a miss would. `CALLER_MEMORY` is the one mode that does
not touch the cache, because that mode allocates nothing and its exact workspace may not depend on what an earlier
render left behind.

**Stroked text (ST8).** A run's stroke is the stroke of its path, and that is all it is: `gshscene_glyph_run_centerline`
(shape-scene) defines the path from the glyphs' outlines in the run's model space, and `stroke.c` expands it like any other
centerline, so there is no second stroker and no second union. The run's resource id is the centerline id of the cache
key, so a reflowed paragraph (new runs) misses where a cache of glyphs would hit; a per-glyph expansion cached by face,
glyph and style would hit after a reflow, but it is exact only for center alignment (inside and outside clip against the
whole run's fill region), moves the rounding of every outline and gives two definitions, so it is left as a measured
optimization to revisit. What the construction said about the glyphs (a glyph the provider could not give is left out and
has a note) is part of the entry, because a hit that forgot the notes would report less than a miss. The path is built
only on a miss, from the allocator the render was given: the sink's, or under `CALLER_MEMORY` the arena in the caller's memory, whose
measure walks the same calls, so the memory asked for is exact. A part, a cell count or a coordinate past a limit refuses the
whole item. The work of a run's stroke is the path's plus one step for each element of the glyph outlines in it, counted on
a hit as on a miss; the declared bound's expansion term is therefore the resource limit times the depth cap plus one. A
stroke never reads the hinted route: the fill of the same run on a grid-aligned map at an integer ppem takes the fast
tier and is grid-fitted, and the stroke is centered on the unhinted outline, so the two can disagree by up to about half
a pixel; hint mode `NONE` on the run asks for a registered pair.

**Fast tiers (SC8).** The list of tiers, each with its detection, its counter and the gate that holds it equal to the
general tier (which can be forced, and is the oracle):

| Tier | Detection | Counter |
| --- | --- | --- |
| FT-1 cached expansion | the key is in the cache | `cache_hits` |
| FT-2 sparse composite, solid hairline | a solid paint, one part, a bound of the deposits from the edges alone within a sixteenth of the rectangle's pixels and within `max_cells`; device width 1 | `sparse_hairline` |
| FT-2 sparse composite, plain solid stroke | the same, any other width | `sparse_plain` |

A second stroker for hairlines (a polyline offsetter that skips joins and caps) would be a second definition of SG6 to
SG9 and cannot meet 1/16 of a pixel at width 1 (a round join at that width has a radius of half a pixel), so there is
none. The cost of a thin stroke lies in its rectangle, which is mostly empty, not in its joins; the sparse composite
drops that cost: it keeps the cell accumulator's rows clean between strokes, notes the first and last column each row
deposited in, sweeps only those columns (and the constant tail, if the row ends inside a shape), and hands the sweep's
bytes to the composite row by row, which composites the nonzero ones. Every skipped pixel has coverage zero and is not
touched by the general loop either. The rectangle the detection reads is the outline's own (the pixels its points
touch), never the damage or the clips, so whether a stroke takes the tier is a function of the stroke. Because every
deposit of a row costs at least a cell, the tier is taken by strokes whose rectangle is large against their edges: long
lines, axis-parallel ones above all (their long edges deposit nothing). A ring, a round join or a cap is a piece of
its own with edges of its own, and counts.

**Work.** A stroke counts the points of its parts, its cell deposits, the pixels of its rectangle (twice when it
aligns), the expansion's steps, whether the expansion was made or found, and the exact step's (a piece, a meeting of
neighbours, an area and a comparison one each), so that the report a render writes does not depend on the cache or on a
tier. The expansion's steps are bounded by the depth cap for each element of the centerline,
clamped to the resource limit of the store, which is the term the declared cancel bound adds.

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
touched map, the coverage, the clip coverages, the accumulator, and for a list
with a stroke that aligns three more arrays of a byte a pixel) from the sink's
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
region only, never on the address of the caller's memory. A stroke's expansion is geometry like any item's: made from the arena, and
measured by the same conversion; the stroke cache is not used by such a render (SC7).

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

The stroke tests (`test_render_stroke.cpp`): the style's every value drawn and none refused for its style; a stroke at
a whole-pixel translation is the same pixels moved, bit for bit, and another fraction another picture; the test sink's
`outline=` is the hash of the outline drawn; what draws nothing (a singular chain, a singular product of invertible
maps, width 0, a degenerate gradient) with one notice and strict `OK`; the composition against the rational union over
every coverage; a centered stroke against a fill of its own body; each limit at its bound (points, cells summed over the
parts and counted whole, a shifted coordinate, a map product); the cache (a hit equals a miss, the bound, the eviction
by id, destruction, a bound of 1, an entry larger than the bound, caller memory neither reading nor writing it, threads);
the tiers over the stroke fixtures on all four formats against the forced general tier, each taken; damage over aligned
and centered strokes; groups; a stroked run refused for its centerline; the work within the declared bound.
`tests/stroke_gate.h` is the bracket against the geometric oracle that shape-geometry installs
(`<ghoti.io/shape-geometry/testing/stroke_oracle.h>`), and the union gate with its independent instrument
(`test_render_stroke_bracket.cpp`: every case within `o = 1/255` of the bracket and a level and a half of the union, the
build without the exact step failing both, the instrument planted with the sum of the pieces failing its self-check against
closed forms). The exact step (`test_render_exact.cpp`): closed forms (nested, crossing, coincident edges, through a lattice
point, a contour that touches the left side, a vertical piece on it, a cell of flat pieces only); random sets of pieces
against the instrument; a window equal to the whole render there and the order of the pieces changing nothing; the sparse
sweep equal to the dense one; three parts together against the instrument, a window equal to the whole, one part's pieces
alone against SC5's formula with constants, the off-centre strip the formula gets wrong; the cap at 512 and 513 and at small
values, over the walk and not the window; no winding as a limit (255, 256, 475, 1024 and deeper); the field `max_exact_fragments` through the sink (validated, defaulted, refused at its bound, damage, strict,
caller memory).

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
