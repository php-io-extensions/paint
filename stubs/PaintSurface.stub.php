<?php

/** @generate-class-entries */

/**
 * Pixels that shapes are painted into: width x height, four bytes each in one of
 * four byte orders, top row first, colours straight (not premultiplied).
 *
 * Coordinates are numbers in pixels with whole numbers on pixel edges, so the
 * first pixel covers 0..1 both ways. A shape is an op: a list that starts with
 * its kind (PAINT_PATH and the others, each documented on its constant).
 *
 * With anti-aliasing a pixel is covered by the share of its area inside the
 * shape, worked out exactly, not sampled. Without, a pixel is in
 * when its centre is. A covered pixel takes the paint's colour by
 * alpha * coverage: channel = (colour * a + pixel * (255 - a) + 127) / 255,
 * and its alpha moves toward 255 the same way.
 *
 * Every number is finite with magnitude at most PAINT_LIMIT; anything else, or
 * an op that is not one, is ValueError.
 *
 * @not-serializable
 */
final class PaintSurface
{
    /** A surface with its own pixels, every byte 0. Both sides 1..PAINT_MAX_SIDE. */
    public function __construct(int $width, int $height, int $order = PAINT_ORDER_RGBA) {}

    /**
     * A surface over pixels something else holds: nothing is allocated or copied, and
     * the bytes are never freed here. $owner is kept alive for as long as the surface is.
     *
     * The address is trusted: it must be the first byte of $height rows, $stride bytes
     * apart, each of at least $width * 4 bytes, valid while $owner lives.
     *
     * @param int $address Not 0.
     * @param int $stride At least $width * 4.
     */
    public static function over(int $address, int $width, int $height, int $stride, int $order, object $owner): PaintSurface {}

    /**
     * The gradient paint fill() takes, [kind, spread, ramp, a, b, c, d, e, f], from a gradient given in a shape's
     * units and the transform the shape is drawn through. The ramp: for each of 256 steps x = i / 255 the last stop
     * at or before x, each channel interpolated straight to the next stop and rounded half up. The six numbers: the
     * transform undone, then the gradient's own map (linear: along the line from (x0, y0) over its squared length;
     * radial: from the centre over the radius). Null when the transform or the geometry cannot be undone.
     *
     * @param int $kind PAINT_LINEAR or PAINT_RADIAL.
     * @param int $spread PAINT_PAD, PAINT_REPEAT or PAINT_REFLECT.
     * @param array $geometry [x0, y0, x1, y1] or [cx, cy, r].
     * @param array $stops [[offset, 0xRRGGBBAA], ...]: offsets 0..1, not falling, at least one stop.
     * @param array $matrix [a, b, c, d, e, f]: x' = a x + c y + e, y' = b x + d y + f.
     */
    public static function gradient(int $kind, int $spread, array $geometry, array $stops, array $matrix): ?array {}

    public function width(): int {}

    public function height(): int {}

    /** The pixels, rows top first and tight: width * height * 4 bytes. */
    public function bytes(): string {}

    /** Every pixel of the rect inside the surface set to $rgba (0xRRGGBBAA), not blended. */
    public function clear(int $rgba, int $x, int $y, int $width, int $height): void {}

    /**
     * Ops painted in order, each with its paint, nothing outside the rect ($x, $y, $width, $height).
     *
     * A paint is a colour 0xRRGGBBAA, or a gradient [kind, spread, ramp, a, b, c, d, e, f]:
     * kind PAINT_LINEAR or PAINT_RADIAL, spread PAINT_PAD, PAINT_REPEAT or PAINT_REFLECT,
     * ramp PAINT_RAMP_BYTES bytes, and six numbers that take a pixel centre (px, py) to
     * gradient space: u = a * px + c * py + e, v = b * px + d * py + f. The colour is the
     * ramp's at t = u (linear) or the length of (u, v) (radial), 0 its first and 1 its last.
     *
     * An op that is not one is ValueError; the ops before it have been painted.
     *
     * @param array $ops A list of ops.
     * @param array $paints A list of paints, one an op.
     * @return array For each op the rect [x, y, width, height] it covered, null for none.
     */
    public function fill(array $ops, array $paints, int $x, int $y, int $width, int $height, bool $antialias = true): array {}
}
