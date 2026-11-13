<?php

/** @generate-class-entries */

/**
 * A filled path: [PAINT_PATH, contours, rule]. Each contour is a flat list of numbers
 * x0, y0, x1, y1, ... and closes itself; rule is PAINT_NON_ZERO or PAINT_EVEN_ODD.
 *
 * @var int
 * @cvalue PAINT_PATH
 */
const PAINT_PATH = UNKNOWN;

/**
 * [PAINT_ELLIPSE, cx, cy, rx, ry]
 *
 * @var int
 * @cvalue PAINT_ELLIPSE
 */
const PAINT_ELLIPSE = UNKNOWN;

/**
 * The band between radii r - stroke / 2 and r + stroke / 2: [PAINT_RING, cx, cy, rx, ry, stroke]
 *
 * @var int
 * @cvalue PAINT_RING
 */
const PAINT_RING = UNKNOWN;

/**
 * A box with rounded corners: [PAINT_ROUND_RECT, x0, y0, x1, y1, [tl, tr, br, bl]]
 *
 * @var int
 * @cvalue PAINT_ROUND_RECT
 */
const PAINT_ROUND_RECT = UNKNOWN;

/**
 * The band inside a rounded box, inset wide: [PAINT_ROUND_RING, x0, y0, x1, y1, [tl, tr, br, bl], inset]
 *
 * @var int
 * @cvalue PAINT_ROUND_RING
 */
const PAINT_ROUND_RING = UNKNOWN;

/**
 * A rounded box with a soft edge, blur wide each side of it: [PAINT_SHADOW, x0, y0, x1, y1, radius, blur]
 *
 * @var int
 * @cvalue PAINT_SHADOW
 */
const PAINT_SHADOW = UNKNOWN;

/**
 * One-pixel lines through points: [PAINT_POLYLINE, [x0, y0, x1, y1, ...], closed]
 *
 * @var int
 * @cvalue PAINT_POLYLINE
 */
const PAINT_POLYLINE = UNKNOWN;

/**
 * @var int
 * @cvalue PAINT_NON_ZERO
 */
const PAINT_NON_ZERO = UNKNOWN;

/**
 * @var int
 * @cvalue PAINT_EVEN_ODD
 */
const PAINT_EVEN_ODD = UNKNOWN;

/**
 * A gradient along x in gradient space.
 *
 * @var int
 * @cvalue PAINT_LINEAR
 */
const PAINT_LINEAR = UNKNOWN;

/**
 * A gradient by distance from the origin of gradient space.
 *
 * @var int
 * @cvalue PAINT_RADIAL
 */
const PAINT_RADIAL = UNKNOWN;

/**
 * Past its ends a gradient holds its end colour.
 *
 * @var int
 * @cvalue PAINT_PAD
 */
const PAINT_PAD = UNKNOWN;

/**
 * @var int
 * @cvalue PAINT_REPEAT
 */
const PAINT_REPEAT = UNKNOWN;

/**
 * @var int
 * @cvalue PAINT_REFLECT
 */
const PAINT_REFLECT = UNKNOWN;

/**
 * A pixel's four bytes in memory, red first.
 *
 * @var int
 * @cvalue PAINT_ORDER_RGBA
 */
const PAINT_ORDER_RGBA = UNKNOWN;

/**
 * @var int
 * @cvalue PAINT_ORDER_BGRA
 */
const PAINT_ORDER_BGRA = UNKNOWN;

/**
 * @var int
 * @cvalue PAINT_ORDER_ARGB
 */
const PAINT_ORDER_ARGB = UNKNOWN;

/**
 * @var int
 * @cvalue PAINT_ORDER_ABGR
 */
const PAINT_ORDER_ABGR = UNKNOWN;

/**
 * @var int
 * @cvalue PAINT_MAX_SIDE
 */
const PAINT_MAX_SIDE = UNKNOWN;

/**
 * The bytes of a gradient's ramp: 256 colours, four bytes each, red, green, blue, alpha.
 *
 * @var int
 * @cvalue PAINT_RAMP_BYTES
 */
const PAINT_RAMP_BYTES = UNKNOWN;

/**
 * The largest magnitude a coordinate may have.
 *
 * @var float
 * @cvalue PAINT_LIMIT
 */
const PAINT_LIMIT = UNKNOWN;
