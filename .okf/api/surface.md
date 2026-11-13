---
type: API
title: Surface
description: Constants, PaintSurface: every method, the ops, the paints, how coverage and blending are worked, and what it refuses.
resource: stubs/
tags: [paint, surface, rasterizer, blend, gradient, c]
status: draft
generated: { by: claude-fable/5.2, at: 2026-11-13T12:00:00Z }
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

# Coverage

Every shape is scanned the same way.[^core] For a sample line at height `y` the shape answers the intervals of `x` where the line is inside it. The rows and columns walked are the shape's extent, a pixel more each side, cut to the rect.

* **Hard edges** (`antialias` false): one sample line a row, at `row + 0.5`. An interval `[a, b)` covers columns `ceil(a - 0.5)` up to `ceil(b - 0.5)`: the pixels whose centres are in it. Coverage is 255.
* **Anti-aliased**: `PAINT_SAMPLES` (16) lines a row, at `row + (k + 0.5) / 16`. Each interval adds, to each pixel it crosses, the width of the pixel it covers times 1/16. A pixel's coverage is `floor(clamp(sum, 0, 1) * 255 + 0.5)`.

What a shape answers for a line:

* **Path.** Each edge with `y0 != y1` crosses the line at `x0 + (y - y0) * (x1 - x0) / (y1 - y0)` while `min(y0, y1) <= y < max(y0, y1)`. The crossings are sorted by `x`, then by the edge's place in the path. Walking them, the winding number moves by the edge's direction (non-zero) or by 1 (even-odd); an interval runs from where the line goes inside to where it leaves.
* **Ellipse.** `[cx - h, cx + h)` with `h = rx * sqrt(1 - t²)`, `t = (y - cy) / ry`, when `1 - t² > 0`.
* **Ring.** The outer ellipse's interval, less the inner one's where the line crosses the inner ellipse.
* **Rounded box.** `[x0, x1)` for `y0 <= y < y1`; in a corner's band the end is on that corner's quarter circle.
* **Rounded ring.** The outer box's interval, less the inner box's.

A shadow is not scanned by intervals. Each pixel centre's signed distance `d` to the rounded box gives coverage `1 - smoothstep(-blur, blur, d)`; with `blur` 0, 1 when `d < 0`. With hard edges the byte is 255 from 128 up, else 0. Columns more than `ceil(radius + blur) + 2` inside both sides share the value at the box's centre column.

A polyline's pixels: along a line's longer axis from its lower end, step `i` moves `floor((2 * minor * i + major) / (2 * major))` across. A pixel two lines share is painted once.

# Blending

A run of pixels at coverage `c` under a colour of alpha `A` takes `a = (A * c + 127) / 255` (whole-number division). `a` 0 changes nothing; `a` 255 writes the colour with alpha 255. Otherwise each channel is `(colour * a + pixel * (255 - a) + 127) / 255` and the pixel's alpha is `(255 * a + alpha * (255 - a) + 127) / 255`.

A gradient does the same a pixel at a time with the ramp entry's colour and alpha.

# Refusals

`ValueError`, naming the op by its place in the list: an op that is not a list starting with a kind; a kind that is not one; a number that is not an int or a float, is not finite, or is past `PAINT_LIMIT` (2³⁰); a contour or point list of odd length; radii that are not four numbers; ops and paints of different lengths; a colour outside 0..0xFFFFFFFF; a gradient that is not nine entries, or has a kind or spread that is not one, a ramp that is not 1024 bytes, or a number that is not finite.

`fill()` reads and paints one op at a time, so the ops before a refused one have been painted.

A surface's sides outside 1..`PAINT_MAX_SIDE`, a byte order that is not one, `over()` with address 0 or a stride under `width * 4`: `ValueError`.

[^stubs]: `stubs/PaintSurface.stub.php`, `stubs/paint.stub.php`.
[^core]: `scan()` in `src/core.c`.
