---
type: API
title: Surface
description: Constants, PaintSurface: every method, the ops, the paints, how coverage and blending are worked, and what it refuses.
resource: stubs/
tags: [paint, surface, rasterizer, blend, gradient, c]
status: draft
generated: { by: claude-opus/5.5, at: 2026-11-18T21:30:00Z }
sources:
  - id: stubs
    resource: stubs/
    title: paint.stub.php, PaintSurface.stub.php
  - id: core
    resource: src/core.c
    title: The painting core
---

# Overview

`PaintSurface` is width x height pixels, four bytes each, top row first, colours straight (not premultiplied). `fill()` paints a list of ops into it, each with a paint, inside one rect.[^stubs]

Coordinates are numbers in pixels with whole numbers on pixel edges: the first pixel covers 0..1 both ways and its centre is (0.5, 0.5).

# Methods

| Method | What it does |
|---|---|
| `__construct(width, height, order = PAINT_ORDER_RGBA)` | A surface with its own pixels, every byte 0. Sides 1..`PAINT_MAX_SIDE` (32767). |
| `PaintSurface::over(address, width, height, stride, order, owner)` | A surface over memory something else holds. Nothing is allocated or copied; `owner` is kept alive; the bytes are never freed. `stride` is at least `width * 4`. |
| `width()`, `height()` | The sides. |
| `bytes()` | The pixels as a string, rows tight: `width * height * 4` bytes. |
| `clear(rgba, x, y, width, height)` | Every pixel of the rect inside the surface set to the colour. Not blended. |
| `fill(ops, paints, x, y, width, height, antialias = true)` | Each op painted in order with its paint, nothing outside the rect. Answers, for each op, the rect `[x, y, width, height]` it covered, or null. |

The byte order says where each channel is in a pixel's four bytes: `PAINT_ORDER_RGBA`, `_BGRA`, `_ARGB`, `_ABGR`.

# Ops

An op is a list that starts with its kind.

| Kind | Op |
|---|---|
| `PAINT_PATH` | `[kind, contours, rule]`. A contour is a flat list `x0, y0, x1, y1, ...` and closes itself. `rule` is `PAINT_NON_ZERO` or `PAINT_EVEN_ODD`. |
| `PAINT_ELLIPSE` | `[kind, cx, cy, rx, ry]` |
| `PAINT_RING` | `[kind, cx, cy, rx, ry, stroke]`: the band between radii `r - stroke / 2` and `r + stroke / 2`. |
| `PAINT_ROUND_RECT` | `[kind, x0, y0, x1, y1, [tl, tr, br, bl]]` |
| `PAINT_ROUND_RING` | `[kind, x0, y0, x1, y1, [tl, tr, br, bl], inset]`: the band inside the box, `inset` wide. The inner box's radii are `max(0, r - inset)`. |
| `PAINT_SHADOW` | `[kind, x0, y0, x1, y1, radius, blur]`: a rounded box whose edge fades over `blur` each side of it. |
| `PAINT_POLYLINE` | `[kind, [x0, y0, x1, y1, ...], closed]`: one-pixel lines through the points, each floored to its pixel. |

A shape with no size (a radius, stroke or inset of 0 or less, a box with no width) paints nothing. The radii are used as given: a caller that wants them held to half the box holds them first.

# Paints

A paint is a colour `0xRRGGBBAA`, or a gradient `[kind, spread, ramp, a, b, c, d, e, f]`:

* `kind`: `PAINT_LINEAR` or `PAINT_RADIAL`.
* `spread`: `PAINT_PAD` (hold the end colour), `PAINT_REPEAT`, `PAINT_REFLECT`.
* `ramp`: `PAINT_RAMP_BYTES` (1024) bytes, 256 colours of red, green, blue, alpha.
* Six numbers that take a pixel centre `(px, py)` to gradient space: `u = a * px + c * py + e`, `v = b * px + d * py + f`.

The colour is the ramp's at `t = u` (linear) or `hypot(u, v)` (radial), entry `floor(t * 255 + 0.5)` after the spread: pad clamps `t` to 0..1, repeat takes `t - floor(t)`, reflect takes `w = t - 2 * floor(t / 2)` and `2 - w` when `w > 1`.

Each pixel's place is worked out on its own: `px = x + 0.5` for the pixel's own column, `u = (a * px + c * py) + e`, not stepped along a span, so where a span starts does not move a bit.

## Building a gradient

`PaintSurface::gradient(kind, spread, geometry, stops, matrix)` makes that paint from a gradient given in a shape's units and the transform the shape is drawn through, `matrix = [a, b, c, d, e, f]` with `x' = a x + c y + e`, `y' = b x + d y + f`. Null when the transform or the geometry cannot be undone.

* **Ramp.** For `i` = 0 to 255, `x = i / 255`; the stops are walked forward to the last at or before `x`. Before the first stop, its colour; from the last stop on, the last's. Between stop `k` and `k + 1`, `u = (x - o[k]) / (o[k+1] - o[k])` and each channel is `floor(a + (b - a) * u + 0.5)`.
* **The transform undone.** `det = a d - b c`, null when 0; `inv = [d/det, -b/det, -c/det, a/det, (c f - d e)/det, (b e - a f)/det]`.
* **The gradient's own map.** Linear `[x0, y0, x1, y1]`: `dx = x1 - x0`, `dy = y1 - y0`, `l = dx dx + dy dy` (null when 0), `g = [dx/l, -dy/l, dy/l, dx/l, -(x0 dx + y0 dy)/l, (x0 dy - y0 dx)/l]`. Radial `[cx, cy, r]` (null when `r` is not above 0): `g = [1/r, 0, 0, 1/r, -cx/r, -cy/r]`.
* **Together.** `m0 = g0 inv0 + g2 inv1`, `m1 = g1 inv0 + g3 inv1`, `m2 = g0 inv2 + g2 inv3`, `m3 = g1 inv2 + g3 inv3`, `m4 = (g0 inv4 + g2 inv5) + g4`, `m5 = (g1 inv4 + g3 inv5) + g5`; null when one is not finite.

Lustrous PHP's drawer works the same steps in PHP, byte for byte.

# Coverage

Two ways, by `antialias`.[^core]

**Hard edges** (`antialias` false). For the line through a row's centres, at `row + 0.5`, the shape answers the intervals of `x` where the line is inside it. The rows and columns walked are the shape's extent, a pixel more each side, cut to the rect. An interval `[a, b)` covers columns `ceil(a - 0.5)` up to `ceil(b - 0.5)`: the pixels whose centres are in it. Coverage is 255.

* **Path.** Each edge with `y0 != y1` crosses the line at `x0 + (y - y0) * (x1 - x0) / (y1 - y0)` while `min(y0, y1) <= y < max(y0, y1)`. The crossings are sorted by `x`, then by the edge's place in the path. Walking them, the winding number moves by the edge's direction (non-zero) or by 1 (even-odd); an interval runs from where the line goes inside to where it leaves.
* **Ellipse.** `[cx - h, cx + h)` with `h = rx * sqrt(1 - t²)`, `t = (y - cy) / ry`, when `1 - t² > 0`.
* **Ring.** The outer ellipse's interval, less the inner one's where the line crosses the inner ellipse.
* **Rounded box.** `[x0, x1)` for `y0 <= y < y1`; in a corner's band the end is on that corner's quarter circle.
* **Rounded ring.** The outer box's interval, less the inner box's.

**Anti-aliased.** A pixel's coverage is the exact area of it inside the shape: nothing is sampled. A shape is its boundary, straight pieces and arcs of ellipses, each adding a winding `w` to everything on its right. The rect is the box: what a piece adds left of the rect's first column is added to that column, and what it adds at or past the rect's right side is never read.

* **A straight piece inside a row**, `h` high, from `xa` at its top to `xb` at its bottom (`xl`, `xr` the smaller and larger): with `xl >= right` it adds nothing; with `xr <= left` it adds `w * h` at `left`. With `il = floor(xl) == ir = floor(xr)`, `m = h * ((xa + xb) / 2 - il)` and it adds `w * (h - m)` at `il`, `w * m` at `il + 1`. Otherwise `rise = h / (xr - xl)`; if `il < left` it adds `w * (left - xl) * rise` at `left` and starts at `left`; then for each `i` up to `min(ir, right - 1)`: `xs = max(xl, i)`, `xe = min(xr, i + 1)`, `hi = (xe - xs) * rise`, `m = hi * ((xs + xe) / 2 - i)`, adding `w * (hi - m)` at `i` and `w * m` at `i + 1`.
* **A straight edge** from `(x0, y0)` to `(x1, y1)`, top first (`w` 1 going down, -1 going up), `slope = (x1 - x0) / (y1 - y0)`, cut to the rect's rows: in each row, from `ya = max(from, row)` to `yb = min(to, row + 1)`, the piece from `x0 + (ya - y0) * slope` to `x0 + (yb - y0) * slope`, `yb - ya` high.
* **An arc**, one side of an ellipse (`side` -1 left, 1 right) between two heights, cut to `cy ± ry` and the rect's rows. In each row it is split at `cy`, so `x` runs one way in each part. A part from `ya` to `yb`: `ua = clamp((ya - cy) / ry, -1, 1)`, `ub` the same, `sa = sqrt(1 - ua * ua)`, `sb` the same, `xa = cx + side * rx * sa`, `xb` the same. The edge cases at `left` and `right` are as for a straight piece. The area under the circle from the centre line is `p = 0.5 * (u * s + asin(u))` at each end, `area = side * rx * ry`. In one pixel, `m = (cx - il) * h + area * (pb - pa)`, added as for a straight piece. Across several, it is cut where `x` crosses each pixel side `k`: `t = (k - cx) / rx`, `u = ±sqrt(1 - t * t)` (the sign of the half it is in), `y = cy + ry * u`, `p = 0.5 * (u * fabs(t) + asin(u))`; each pixel `i` between two cuts takes `hi = fabs(y2 - y1)`, `m = (cx - i) * hi + area * (p2 - p1)` (or `p1 - p2` as `y` falls), added as for a straight piece.
* **A row's coverage**: the running sum of what was added, column by column from the left; `c = min(|sum|, 1)`, the byte `(int) (c * 255.0 + 0.5)`.

What a shape's boundary is, in the order its pieces are added:

* **Path.** Each row is settled first. It is cut at the heights where an edge starts or ends inside it. Between two cuts, the edges spanning the stretch are taken at its top and bottom and sorted by the sum of the two `x`, then by their place among the edges; if two cross inside the stretch (one left of the other at one end and right at the other) it is cut again at `from + (to - from) * top / (top - bottom)`. Walking the sorted edges, the winding moves by each edge's direction; where inside (non-zero: winding not 0; even-odd: winding odd) turns on or off, that edge is added as a straight piece with `w` 1 or -1. A row with no cuts and exactly two edges, one each way, that do not cross in it, adds both whole, with their own directions.
* **Ellipse.** Its left side with `w` 1, its right side with `w` -1.
* **Ring.** The ellipse half the stroke larger, as above; then, when `rx - stroke / 2` and `ry - stroke / 2` are above 0, the one half the stroke smaller with the windings the other way.
* **Rounded box.** Its left side with `w` 1, then its right with `w` -1. Down a side: the top corner's quarter circle to `min(y0 + r, y1)`, the straight part to `max(y1 - r, that)`, the bottom corner's quarter circle.
* **Rounded ring.** The box, then the inner box with the windings the other way when it has width and height left.

A shadow is neither. Each pixel centre's signed distance `d` to the rounded box gives coverage `1 - smoothstep(-blur, blur, d)`; with `blur` 0, 1 when `d < 0`. With hard edges the byte is 255 from 128 up, else 0. Columns more than `ceil(radius + blur) + 2` inside both sides share the value at the box's centre column.

A polyline's pixels: along a line's longer axis from its lower end, step `i` moves `floor((2 * minor * i + major) / (2 * major))` across. A pixel two lines share is painted once.

# Blending

A run of pixels at coverage `c` under a colour of alpha `A` takes `a = (A * c + 127) / 255` (whole-number division). `a` 0 changes nothing; `a` 255 writes the colour with alpha 255. Otherwise each channel is `(colour * a + pixel * (255 - a) + 127) / 255` and the pixel's alpha is `(255 * a + alpha * (255 - a) + 127) / 255`.

A gradient does the same a pixel at a time with the ramp entry's colour and alpha.

# Refusals

`ValueError`, naming the op by its place in the list: an op that is not a list starting with a kind; a kind that is not one; a number that is not an int or a float, is not finite, or is past `PAINT_LIMIT` (2³⁰); a contour or point list of odd length; radii that are not four numbers; ops and paints of different lengths; a colour outside 0..0xFFFFFFFF; a gradient that is not nine entries, or has a kind or spread that is not one, a ramp that is not 1024 bytes, or a number that is not finite.

`fill()` reads and paints one op at a time, so the ops before a refused one have been painted.

A surface's sides outside 1..`PAINT_MAX_SIDE`, a byte order that is not one, `over()` with address 0 or a stride under `width * 4`: `ValueError`.

[^stubs]: `stubs/PaintSurface.stub.php`, `stubs/paint.stub.php`.
[^core]: `src/core.c`: `scan()` for hard edges; `area_span`, `area_arc_piece`, `area_row` and `path_settle` for anti-aliased.
