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
flattened by `shape-geometry`'s integer flattener (tolerance 1/16 pixel)
into polylines; a rational curve (a circle or an ellipse, whether written as conics
or as an arc) takes `shape-geometry`'s conic route, which holds it within the
tolerance less 0.29 units by a proved rule. Each subpath is closed (an open one is filled as if closed). Every edge deposits two signed numbers into each cell it crosses:
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

### Masks (MC1 to MC10)

A group whose soft-mask slot names a mask is composited at its pop with the mask's value weighing each pixel (the rules are in
`render.h`; the meaning of a mask, MK1 to MK14, is shape-scene's). The decisions that shape the code:

**The mask is drawn at the pop, over the pop region.** The only moment the sink knows which pixels will be composited is the
pop, so the mask's picture is walked then, by an expansion of its own, into one extra layer and over those pixels only. A small
masked icon on a large target pays for the icon, a mask outside the group's bounds is never drawn, and the frame is live only
while the pop runs, so masks nested to a depth cost that depth along one path. The walk is the main walk's units
(`unit()` is called on the picture's items with a chain that names the group's push and the item in the picture), restricted by
a **window** that every reach (the rectangle an item may draw in) is met with: the region in the main walk, the pop region in a
mask pass. Nothing else in the sink knows it is drawing a mask.

**The value is the waist's, with no rounding.** `gshcpu_render_mask_value` (`mask.c`) orders MK2's arithmetic in `double`
and calls `gshscene_mask_resolve` and `gshscene_mask_transfer` for everything else: the coefficients, the spaces, the transfer.
The sink holds no transfer evaluator and no table. The value meets the pop's composite unrounded, and the pop is the pop of any
group: `apply` with the source scaled by `o v`, or the weight `(m8 / 255) v` for a mask on the shape. With `v = 1` the products
are `o` and `m8 / 255` exactly, so a mask of value one is the same bits as no mask (a test says so for both acts-on, four
operators and every format). A pixel whose weight is zero under `SHAPE` is still touched: the pop calls `apply` for it, with a
weight of zero, and `apply` with a zero weight is the identity on the value, so the rule needs no case of its own (and the oracle
states the same). The cost is that such a pixel is visited and marked where a skipped one would not be, and, in a straight-alpha
target, that its bytes can differ by the rounding of the round trip from what `OPACITY` with a value of zero leaves (a bounded
operator whose scaled source is zero composites nothing, so that pixel stays untouched).

**A list holding a masked group is composited in double layers, whatever the target's format.** The fast fill coverage holds the
layers of an 8-bit target as bytes. A mask's value weighs the group's pixels, and a layer quantized to 8 bits before the weight
is applied is not the mask: the quantization is a second source of error in a value that the exact-rational reference bounds
to `1e-12`, and that the sink meets to 1.1e-16 (the worst error before the write, over the render-level vectors). There is therefore
no 8-bit-layer path for a masked group, and a list that holds one has the byte tiers off, as one holding an image paint does (the
fast coverages of fills and strokes still apply to the items); the layers cost eight bytes a component instead of one, which the
layout counts (the next paragraphs).

**Limits are the whole render's.** The cell cap is met over an item's rectangle met with the whole render's region, whatever
the damage (S4). A mask's pop region depends on the group's dirty rectangle, which a damaged render knows only in part, so the
cap for an item in a mask picture could not be taken over `P` without making a refusal depend on what was drawn around it.
The cap is therefore taken as for any item (over the whole region), and that over-counts for a small group, deliberately: the
bound is the safe side, and deterministic. What also has to match is whether the mask is drawn at all: the whole render draws it
when its pop region is not empty, which a damaged render cannot tell from its own dirty rectangle (the group's items outside
the damage draw nothing there). Each layer therefore carries a second rectangle, the pixels the whole render's units reach of
it (the same `widen` calls, with the whole render's rectangle of each item), kept only in a list that holds a mask. A damaged
render draws a mask whenever the whole render would, over an empty window if its own pop region is empty, and so refuses
what the whole render refuses and reports the same entries (a test finds the least cap the whole render passes and requires every
window, including one that holds none of the group, to meet the same cap).

**Memory is counted as groups are, and a list with no mask is laid out as before.** The layout adds, at each masked group,
the mask frame and everything the picture needs while it is drawn (its groups and clips, its gradients and strokes, and a
masked group inside it one frame deeper), found once for a picture, however many masks name it and however many pictures there are, by a memo of the pictures seen that
grows with them (from the sink's allocator, or, under `CALLER_MEMORY`, which allocates nothing, from the caller's buffer before
anything else lives in it; a table with no room left forgets, and a picture past it is counted again where used: slower, never
different. Rendered targets are large next to a table, which takes about 640 bytes a picture at most, so the room is there in practice). The walks of the pictures of
masks being drawn are stacked in one area, the frame records and the chain of a unit beside them. That area is carved after the
`Run`, directly, and only when the list has a mask, and the flag that says so takes padding the `Run` already had: the
need for memory, the work counts and the units of every fixture from before masks are unchanged (a test pins them).

**Cancel between the mask's units.** The pass reports each unit's work to the job and checks the token after it, so the
declared bound is still the worst single unit's, a unit of the mask or the pop's composite (two passes over its region: the
visit and the value). The expansion count and the progress units do not count a mask's units, so no other sink's contract changes.

**Two tiers, each the general formula with something folded.** A pixel outside the mask frame's dirty rectangle has the layer
`(0, 0, 0, 0)`, so its value is one constant per pop, found by the same function on zeros; an identity transfer is not called.
The test forces each off and compares every mask fixture on all four formats bit for bit, and counts each taken on the corpus.

**Measured.** The render-level vector gate (246 cases on four formats, from a generator that restates the rules in exact
rationals and shares nothing with the library or with shape-scene's oracle): worst error before the write 1.1e-16 against the
`1e-12` bound, 204 stored values exact; nine fixtures add 72 goldens (36 exact, 36 under the fast coverages) and move no earlier
one. On the corpus the 60 masked pops took FT-M1 in 29 and FT-M2 in 48.

### Non-isolated and knockout groups (KC1 to KC11)

A **general layer** is the layer of a group that is non-isolated or knockout. T3 to T5 of the waist's transparency model say what its
pixels are; this section says how the reference computes them, and why the cost falls only on the groups that set a flag.

**One operator, rewritten.** T3 writes an element's step as `(1 - f) L + (f - Aa) Bk + T`, which needs the blend term `T` and two
backdrop terms. `T + (1 - Aa) Bk` is what the plain compositor's operator makes of `A = f S` and the backdrop (`op_X(A, Bk)`, B4),
so the layer becomes `(1 - f)(L - Bk) + op_X(A, Bk)`. It is the same function in the reals, and the vector generator checks the
identity in exact rationals at every step of every case. It means one operator serves both paths: with `Bk = L` the subtraction is
exactly 0 and the step is the plain `apply`, bit for bit, so the layers that need none of this are not routed through anything new,
and a plain layer under a knockout group, which has to step its group shape, still composites exactly as it did. The operators that
are not PDF's take `(1 - f) L + f op(S, Bk)` with the backdrop in the operator only.

**The backdrop is read, not copied.** A non-isolated group's initial backdrop is the parent's pixel at the push (or the parent's own
initial backdrop where the parent is knockout), converted into the group's space. The parent does not change while the group is open,
so a read at any time gives the bits a copy at the push would have: the sink reads it where a layer is made ready, where an element of
a knockout group needs it, and at the pop, through the parents that are knockout, with one conversion prepared at the push. That is why
a group costs no memory for a backdrop. It also fixes a requirement: the parent is made ready over a pixel before the child is, which
`widen` does (the parent's dirty rectangle grows to the child's), so inside a damage the render equals the whole render.

**A pixel nothing reached is exactly empty.** The layer of a non-isolated group is made ready as a copy of its initial backdrop, with a
group alpha of 0. At the pop `G = L - (1 - Ag) L0` is then a difference of two equal doubles, 0 exactly, so a bounded operator skips
the pixel and the pop region is the dirty rectangle, as for an isolated group. A design that kept the backdrop in another
representation (a copy, a differently rounded conversion) would leave a residue of `1e-16` at every pixel and make every
non-isolated group look unbounded.

**The channels.** A non-isolated group carries a group alpha. An isolated knockout group does not: its initial backdrop is 0, so its group alpha is
the alpha of its layer at every step. A group carries a group shape only when some ancestor is knockout, because the shape reaches
a result only as the shape of a pop in a knockout parent, and a parent's own shape is made of its elements' shapes, this group's pop
among them. The layout counts the two maxima over the expansion with a stack of the open groups that are non-isolated or knockout (a
plain group needs no entry, so a list of plain groups never allocates for it), recursing into instances, which are drawn into the layer
they sit in, and into mask pictures, each of which is its own isolated group and so starts again with no knockout ancestor.

**What the story's skip rule had to change.** A bounded operator with a source of nothing leaves the layer alone where the backdrop is
the layer. In a knockout group it does not: an element of no opacity but some shape removes its shape's share of what the layer holds
beyond its initial backdrop (T3 with `A = 0`: PDF's "a transparent object knocks out"). The step skips it only when its shape is 0 as
well, and the group shape steps whatever the source.

**Conflation is the definition (KC11).** In a knockout group two abutting shapes each covering half of a pixel leave a quarter of the
initial backdrop showing, where an exact union would leave none. A knockout is defined on shape, shape is coverage, and `SEAMLESS`
is not claimed. The test pins the values so that a fix would be seen.

**The default path.** `GSHCPU_RENDER_FORCE_GENERAL` (a test switch, not a tier) routes every layer, the root's too, through the
element step. Every fixture of the corpus on all four formats agrees with the normal render: all but two of the 224 are the same
bytes, and those two differ by a rounding at the one write. A list without these groups takes none of the new code and has the layout, the
work counts, the pixels and the goldens it had.

**Measured.** The render-level gate against the third restatement of the rules (672 cases, 210 exact, over four combinations, eleven
operators, three backdrops, clips, nesting, spaces, abutting shapes and a transparent shape in a child of a knockout group): worst
error before the write 3.3e-16 against the `1e-12` bound; shape-scene's installed `group_vectors.h` through the primitives (52 cases,
worst 1.1e-16) and through renders where the coverage is whole; its installed `mask_vectors.h` (485 cases, 156 exact, worst 6.7e-16: every
mode, transfer and backdrop of a mask under each of the four parents, so `SHAPE` against `OPACITY` is shown for source-over and the
blend modes under a knockout parent, and for the operators that are not bounded under every parent) through the primitives; seven
fixtures add 28 goldens and move no earlier one.

**A paint of the zero space inside a mask's picture.** A paint of the zero space is the render target's. Inside a mask's picture the
sink converts it from the target's space into the mask's render space `R`, as it converts any paint into the space of the group it is
painted in. That is what ISO 32000-1 says of a transparency group (Table 147 makes a group's color space the one colors are
converted into when painted into the group, and 11.3.4 converts source colors to the blending color space), and a soft mask is derived
from such a group (11.6.5.2); CSS Masking likewise converts only at the luminance step. shape-scene's oracle draws the
picture the same way, so the installed `mask_vectors.h` and this sink agree on all 485 vectors, among them 24 whose `R` is another space
than the target's (an earlier reading of the oracle took such a paint as already in `R`, and those 24 vectors moved when it was corrected).

### Patterns (PC1 to PC12)

A pattern is a picture repeated on a lattice and the paint that fills with it (shape-scene's PT1 to PT14). The sink draws the
definition, not a resampled tile: for a fill or a stroke painted with a pattern it renders the **lattice picture** into an isolated
layer over the pixels the use draws, and reads that layer as the paint's source, as a gradient is read.

**The pass.** A pattern paint, begun like any paint, makes one *pass* over the tile: the tile's units, walked by an expansion of their
own into one more layer (the mask's picture is walked the same way), each *one element over all the copies*. A unit's coverage in a
copy is the coverage of its outline under the copy's **canonical map** (the chain's linear part and the translation's fraction in
`[0, 1)`; `shape-geometry`'s one function, `gshgeom_lattice_canonical` (LT5), splits the copy's position into whole pixels and that
fraction: LT2's `tau + i a + j b` split by the stroke function, or, where the step vectors are integer vectors, the exact split `floor(tau) +
i a + j b` in integers and the fraction of `tau` itself, because the sum in `double` rounds the fraction once it grows and two copies of a
lattice would then not be one set of bits moved), the outline
cut to the cell's quadrilateral by `shape-geometry`'s convex clip and scan converted over the region moved by the whole pixels. So a
copy is a function of the linear part, the fraction and the shift only; two copies with equal fractions are the same bits moved, and a
paint inside the tile (a gradient, an image, another pattern) is evaluated at the position relative to its own cell. A copy at the
index a million is the copy at zero moved by an exact number of pixels, with no accumulated error.

**One element, many copies (PC4).** The copies of a unit that touch a pixel are summed in lattice order into an accumulator the size of
the region: the weights `m8_x` (the 8-bit coverage after the clips of that copy), their count, and either the one copy's source or the
sum of the sources each times its weight. The unit then acts on the layer as one element with the shape `min(255, sum) / 255` and the
mean of the sources weighed by the coverages (a pixel only one copy touched is N7 exactly). Rounding is the one difference from the text of
PC4: the sum of weighed sources divided by the sum of weights is that text's `(m8_x / sum)` weights to within a rounding of the mean, which is
the form that keeps a single copy equal to the plain fill bit for bit. Each clip of the tile is made again for each copy that
meets it (its outline cut to that copy's cell), and the unit's weight in a copy is its coverage weighed by that copy's own clips; the
union of a clip over the copies (saturating) is what a group inside or around the clip block is weighed by at its pop.

**Overlapping cells (PC6) and blend modes.** When a step is smaller than the cell on an axis that repeats, the copies are drawn one at a
time in lattice order, each with all its units onto the lattice layer (source-over of an isolated copy is drawing it there; a tile that
holds a blend mode is drawn the same way, as one non-isolated, non-knockout group over the lattice layer). The cells' coverage is not
multiplied in again: the outlines were cut to the cell by the clip, and multiplying would square the coverage at a cell's edge.

**Strokes in a tile (PC5)** are cut to the cell exactly. The expansion of the stroke (the cached one, which is a function of the
centerline, the style, the linear part and the fraction) has up to three parts, the body, the aligned part and the clip path; the body and
the aligned part are cut to the cell's quadrilateral by `shape-geometry`'s winding-preserving convex clip (in this copy's stroke only; the
cache's outline is not touched), and SC5 composes the cut parts with the clip path as it composes a stroke's, `k_b + k_a c' - k_b k_a c'`.
The aligned part is cut to the cell and the clip path restricts it, so the composition is the restriction of the stroke's coverage to the
cell and the two sides of a cell edge add to the whole: a butt-capped line from one edge to the other is, copy after copy, the fill of the
rectangle it covers bit for bit (a test), and a line that ends at a cell edge leaves no dent at the seam pixel. The first build weighed the
stroke by the cell's 8-bit coverage instead, which multiplies two 8-bit values at the seam and is exact only where the stroke runs on across the edge. The
cut is bounded as the clip is (the seam area at most `c (sqrt(2)/2) D` for `c` crossings and a cell of diameter `D`, shape-geometry's CK4).

**Limits (PC8).** The copies of a use are counted by `gshgeom_lattice_each` over what the whole render draws of the use, at every level of
nesting, summed, and one more than `GSHCPU_Limits.max_pattern_cells` is `RESOURCE_LIMIT` for the fill or stroke painted with the
pattern: a function of the list and the target, so the same under a damage, with the caller's memory and under any tier. A limit met inside a
tile (a copy's points, its cells, a shifted coordinate past the range) refuses the use, not the unit; the lattice layer is a group layer and meets the limits
a group's does. A damage does not change the count of the outermost use, but the copies of a *nested* pattern are counted only for the outer copies
that are drawn.

**Memory (PC11).** The lattice layer is a group layer in the frame stack, a pass takes an accumulator (37 bytes a pixel), a coverage
buffer and a record, and nests as the patterns do; under `CALLER_MEMORY` all of it is in the caller's block, the geometry of a tile's units
comes from the arena above the item that is painted with the pattern, and the size the sink reports is a bound that is never too small.

**The period tier (PC9).** A pattern whose step vectors `a` and `b` are integer vectors (every component an exact integer below `2^31`,
both axes repeating, `D = a_x b_y - a_y b_x` nonzero and at most `2^31`) has a lattice picture that a translation by any `i a + j b` leaves
unchanged: every copy has the fraction of `tau`, so every copy is the first copy's bits moved by whole pixels. Such a pass is drawn **over a
window of one period**: `gshgeom_lattice_period` gives the Hermite normal form `(p, r, s)` of the lattice (`p s = |D|`) and its reduction
`rep`, and the window `[0, p) x [0, s)` moved to the corner of the lattice layer's rectangle is the part of the layer that is drawn
(by the general code, with the rectangle the window; the restriction property of the scan converter makes the window's pixels the layer's).
Every pixel of the use is then read at `rep(q - w) + w`, `w` the window's corner. The window has to fit in the rectangle (`p` at most its
width, `s` at most its height; so `|D|` is at most its pixels); a lattice whose period is wider than the use is drawn whole. The window is
the layer's own memory, so the tier takes no workspace of its own. It does not apply where the tile holds a pattern (the nested count of
PC8 is of the outer copies drawn) or a soft mask (a mask's picture is placed once in the pass's frame, not once for each copy), and it is
taken for a tile with blend modes, groups and strokes: the value at a pixel is a function of the copies that touch it, in lattice order, which a
translation by a lattice vector preserves. A limit never depends on the tier: a copy's other limits (deposits past the cap, the domain
and the work of a gradient or an image paint, the range of the cell's pixels) are met by the copies of the window over what the whole render
draws of each, and a copy elsewhere is the same bits moved, so the window can vouch for the whole lattice only when no copy could meet a
limit over its whole cell. Each such copy is therefore checked against the *whole cell* (its pixels, the bound of its deposits over the
cell, a gradient's and an image's domain over the cell): if any copy fails, the pass gives the window up (`period_aborts`) and draws the use's
rectangle by the general tier, which decides every copy. The counters `pattern_period` (passes the tier drew) and `period_aborts` are in
`GSHCPU_Render_Counters`, and `GSHCPU_RENDER_TIER_PERIOD` is the tier switch the gates turn off to compare: every fixture, the cases of
`test_render_pattern_period.cpp` and every fuzz input render the same bytes and the same report with it on and off.

**The cache of a period (PC10).** The window's pixels, four doubles each in the order of the classes `rep(q - t)` (`t` the whole pixels of
copy `(0, 0)`, so that a lattice moved by whole pixels is the same entry), are kept in the stroke expansions' table (one `cutil` mutex, least
recently used first, the same `max_cache_bytes` bound, pinned while a render reads one, bypassed under `CALLER_MEMORY`, dropped by
`evict(id)` for the pattern's or the tile's id and by destruction, counted by `static_bytes`). The key is the pattern's and the tile's
resource id, the bit patterns of the canonical map's linear part and fraction, the layer's and the target's color spaces, the list's foreground
and palette when the tile holds a paint that reads them, and the revisions of the stroke expansion, the flattener, the clip and the lattice (each
a constant of `shape-geometry`); the tint, the alpha multiplier, the damage and the region are applied after the lookup and are not in it. A hit
copies the raster into the window and is the miss bit for bit; a raster larger than the bound is drawn and not kept. Ids are a store's: as for the
stroke expansions, a sink that renders the lists of two stores whose ids collide has to be given a table each (`evict`, or a sink for each).

### A refused stroke draws nothing in every tier

The sparse stroke tier paints a band of rows as it sweeps it, so a stroke refused for its pieces or crossings (UC9, UC19) in a later
band would have been left part drawn, where the general tier finds the refusal before it paints anything. A stroke whose outline could
pass either limit (the function of the outline alone that decides the damage-independence of the refusal) therefore takes the general
tier; at the default limits that is no stroke a render draws today. Found by mutating the render fuzzer's seeds; the input is
`regression-sparse-partial-under-budget.bin`.

### Hatches (HX13; masks-images-patterns story H1)

A hatch fill is drawn as the stroke of its trimmed, dashed lines: shape-geometry's hatch (HX4 to HX8) makes the pieces and their outline,
and everything after the outline is the stroke's (SC3 to SC8: the canonical map and its whole-pixel shift, the cache, the cover, the
sparse composite, the clips and the operator). Nothing about the lines is decided in this library; a second implementation of the trim, the
dashes or the pen would be the drift the shared code exists to prevent, and `test_render_hatch.cpp` shows the identity instead: a hatch fill
and the stroke of the same pieces as one explicit path are the same bytes.

The sink does four things of its own. It makes a **synthetic stroke item** (`hatch_fill_item`): the region as the centerline, the hatch in
the style slot (`gshcpu_render_stroke_open_lines` reads the pen from the hatch there), the fill's rule, and as the paint the hatch's ink with
its alpha times the hatch paint's multiplier; the fill's own coverage is not multiplied in, as a stroke's centerline is not. It **keys
the cache** with the hatch's id, the fill rule (the rule `RULE` nesting reads) and the hatch revision besides a stroke's key, so a hatch, a
rule or a fraction changed with the rest held draws the new picture; `evict(id)` takes the hatch's id too. It **counts the lines of the use**
once, from the pieces (or from the entry's meta on a hit, so a hit refuses what a miss would), against `max_hatch_lines`, and keeps the count
in `hatch_lines` beside `hatches` for the tests. And its work bound grows by one step per line per region element, since a line's trim
visits the region.

A hatch inside a pattern's tile is a stroke of the tile (`pat_stroke`), cut to the cell by the same exact clip, so the lines end at the cell
and run on in the next copy.

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
| FT-U1 union clip, disjoint members | the planning predicate shows the members' device boxes interior-disjoint (shape-scene `clip_union.h`, CU16) | `union_fast` (pushes), `union_sweeps` (members), `union_tests` (comparisons) |
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

### Union clips (shapes story A3: CU18 to CU22)

A clip push may name a clip union (shape-scene `clip_union.h`). The sink claims `CLIP_UNION` while `GSHCPU_RENDER_TIER_UNION` is on and never
`CLIP_UNION_GENERAL`, so the planning pass refuses a union whose members it cannot show disjoint (`UNION_OVERLAP`) and `clip_push` only
ever sees a union it can draw exactly. **The arithmetic (CU18).** Each member, taken through `gshscene_clip_union_member_item`, is
swept by the dense cover with the **magnitude fold and no final shift** (`fold_area`, the rule-reduced area in 1/8192 pixel, the number
the byte is the shift of) and added into one `uint16` plane over the union's rectangle, each pixel held at 8192; when the last member
is in, each plane value `s` becomes the clip's byte `min(255, s >> 5)`: the sum is quantized once. The byte path of the cover calls the same
`fold_area` and its bits do not move. The union never uses `GSHCPU_FILL_COVERAGE_FAST`: that mode rounds to 255ths and holds the
magnitude, and a union drawn by it would be a second definition. The fold is of the magnitude, so a member wound the other way gives
the same union (a single clip's signed fold differs by one level in a pixel that is not a whole level; that is recorded as a defect to be
fixed in its own story, not changed here).

**The push (CU19).** `unit()` bills the predicate's comparisons of every union push that is not inside a skipped block, in unit order and
whatever the damage excludes, against `GSHCPU_Limits.max_union_tests` (default 2^26, `UINT64_MAX` no cap); the push at which the running
total passes the limit is `RESOURCE_LIMIT`, and strict fails there. `union_push` sizes the plane from the members' device boxes
(`gshscene_clip_union_member_box`, CU15) met with the clips in force, sweeps one member at a time through the item arena (the geometry of
one member is live at a time) with the cell cap met over the whole render per member, so a damaged render refuses what the whole render refuses,
and a member over a cap refuses the whole push: a union is never drawn with a member left out.

**Memory (CU20).** The plane lives in the item arena (so a list with no union has the memory, the work counts and the pixels it
had): `measure_one` counts the plane (two bytes for each pixel of the region) and then the geometry of the member that takes the most.
**Not built: a union in a pattern's tile (CU21).** `pat_clip_push` answers `ERR_LIMIT` for one, which is the sink's resource limit (PC8)
for the use painted with the pattern, never a union drawn with a member left out; the per-copy plane is a story of its own.
A union in a mask's picture or around or inside a non-isolated or knockout group is an ordinary clip of an ordinary unit: the clip is a
byte coverage by the time a group sees it.

**Tier bookkeeping (CU22).** FT-U1: detection is the planning predicate's verdict; the counters are `union_fast`, `union_sweeps` and
`union_tests`; `GSHCPU_RENDER_TIER_UNION` forces the tier off (the sink then does not claim `CLIP_UNION` and every union is refused
`CLIP_UNION`); the AD-13 pairing, until a general tier exists, is the independent oracle (`tools/clip-union-oracle.py` in shape-scene).

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

A primitive tag never changes coverage. A path resource may carry a tag (a rect, a rounded rect or an ellipse, with flips and the
parameters it was built from; shape-scene's `primitive.h`) that a document writer or an analytic route reads; this sink draws a path
resource by its path. `test_render_primitives.cpp` renders a tagged path and the untagged resource of the same geometry for every
primitive (the rect and the ellipse under all four flip pairs, a rounded rect, a circle), six maps (a rotation, a skew, a mirror, a
stretch, a fractional translation, the identity) and six items (a fill under each rule, a clip, and strokes with each join, cap and
dash), and requires the bits to be equal; it also requires the flips to change a dashed stroke and nothing else, since the dashes
begin at the start corner. `tools/check-tag-ignored.sh` is the other half: no source of the renderer names the accessor, the kind
or the type, and the gate first shows itself failing on a planted call, kind and type. Planted branches that read the tag and
change a render fail the test (`pt-` in `tools/check-planted.py`).

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
