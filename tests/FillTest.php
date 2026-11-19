<?php

declare(strict_types=1);

/*
 * Coverage worked by hand. Hard edges: a pixel is in when its centre is.
 * Anti-aliased: a pixel's coverage is the exact share of its area inside the
 * shape, as a byte: floor(share * 255 + 0.5).
 */

it('fills the pixels whose centres are inside, with hard edges', function (): void {
    $surface = new PaintSurface(4, 4);
    // 0.6..2.4 holds one pixel centre each way: 1.5
    $boxes = $surface->fill([square(0.6, 0.6, 2.4, 2.4)], [0xFF0000FF], 0, 0, 4, 4, false);

    expect(pixel($surface, 1, 1))->toBe([255, 0, 0, 255])
        ->and(pixel($surface, 0, 0))->toBe([0, 0, 0, 0])    // centre 0.5 is outside 0.6
        ->and(pixel($surface, 2, 2))->toBe([0, 0, 0, 0])    // centre 2.5 is outside 2.4
        ->and($boxes)->toBe([[1, 1, 1, 1]]);
});

it('covers a pixel by the area of it inside the shape, anti-aliased', function (): void {
    $surface = new PaintSurface(3, 1);
    // x 0.25..2.5 over the whole row: 0.75, 1, 0.5 of the three pixels
    $surface->fill([square(0.25, 0, 2.5, 1)], [0xFFFFFFFF], 0, 0, 3, 1);

    // alpha 255 * coverage; over a blank pixel each channel is (255 * a + 127) / 255 = a
    expect(pixel($surface, 0, 0))->toBe([191, 191, 191, 191])
        ->and(pixel($surface, 1, 0))->toBe([255, 255, 255, 255])
        ->and(pixel($surface, 2, 0))->toBe([128, 128, 128, 128]);
});

/** The coverage byte of each pixel of a w x h surface after one op in opaque white: over a blank pixel, alpha = coverage. */
function coverage(array $op, int $width, int $height): array
{
    $surface = new PaintSurface($width, $height);
    $surface->fill([$op], [0xFFFFFFFF], 0, 0, $width, $height);

    return array_map(fn (int $i): int => ord($surface->bytes()[$i * 4 + 3]), range(0, $width * $height - 1));
}

it('covers a pixel by the exact area of a curve, not a sampled one', function (): void {
    // a circle inscribed in one pixel covers pi / 4 of it: floor(0.785398 * 255 + 0.5) = 200
    expect(coverage([PAINT_ELLIPSE, 0.5, 0.5, 0.5, 0.5], 1, 1))->toBe([200])
        // each pixel holds a quarter of the band between radii 0.5 and 1: pi * (1 - 0.25) / 4 = 0.589, so 150
        ->and(coverage([PAINT_RING, 1, 1, 0.75, 0.75, 0.5], 2, 2))->toBe([150, 150, 150, 150])
        // each pixel is one corner's quarter circle of radius 1: pi / 4, so 200
        ->and(coverage([PAINT_ROUND_RECT, 0, 0, 2, 2, [1, 1, 1, 1]], 2, 2))->toBe([200, 200, 200, 200]);
});

it('covers a pixel by the exact area of straight edges, crossings settled by the rule', function (): void {
    // the pixel's diagonal: half of it, floor(0.5 * 255 + 0.5) = 128
    expect(coverage([PAINT_PATH, [[0, 0, 1, 0, 0, 1]], PAINT_NON_ZERO], 1, 1))->toBe([128])
        // a bow-tie inside one pixel: two triangles of 1/4, wound opposite ways, so summed signed area would be 0;
        // either rule fills both, 1/2
        ->and(coverage([PAINT_PATH, [[0, 0, 1, 1, 1, 0, 0, 1]], PAINT_NON_ZERO], 1, 1))->toBe([128])
        ->and(coverage([PAINT_PATH, [[0, 0, 1, 1, 1, 0, 0, 1]], PAINT_EVEN_ODD], 1, 1))->toBe([128])
        // a square from far left of the rect to x = 0.5: what is left of the rect is left of its first pixel
        ->and(coverage([PAINT_PATH, [[-5, 0, 0.5, 0, 0.5, 1, -5, 1]], PAINT_NON_ZERO], 2, 1))->toBe([128, 0]);
});

it('settles windings above 1 by the rule, anti-aliased', function (): void {
    // x 0..6 and 2..4 over one row, wound the same way: winding 1, 2, 1, then 0 from x = 6 on
    $nested = fn (int $rule): array => [PAINT_PATH, [[0, 0, 6, 0, 6, 1, 0, 1], [2, 0, 4, 0, 4, 1, 2, 1]], $rule];

    expect(coverage($nested(PAINT_NON_ZERO), 8, 1))->toBe([255, 255, 255, 255, 255, 255, 0, 0])
        ->and(coverage($nested(PAINT_EVEN_ODD), 8, 1))->toBe([255, 255, 0, 0, 255, 255, 0, 0]);
});

it('blends by the colour\'s alpha times the coverage', function (): void {
    $surface = new PaintSurface(1, 1);
    $surface->clear(0x000000FF, 0, 0, 1, 1);
    // alpha 128 at full coverage: a = 128; red = (255 * 128 + 0 * 127 + 127) / 255 = 128
    $surface->fill([square(0, 0, 1, 1)], [0xFF000080], 0, 0, 1, 1);

    expect(pixel($surface, 0, 0))->toBe([128, 0, 0, 255]);
});

it('fills by the rule where a path crosses itself', function (): void {
    // two squares one inside the other, both wound the same way
    $nested = [PAINT_PATH, [[0, 0, 6, 0, 6, 6, 0, 6], [2, 2, 4, 2, 4, 4, 2, 4]], PAINT_NON_ZERO];
    $nonZero = new PaintSurface(6, 6);
    $nonZero->fill([$nested], [0xFFFFFFFF], 0, 0, 6, 6, false);

    $nested[2] = PAINT_EVEN_ODD;
    $evenOdd = new PaintSurface(6, 6);
    $evenOdd->fill([$nested], [0xFFFFFFFF], 0, 0, 6, 6, false);

    expect(pixel($nonZero, 3, 3)[3])->toBe(255)
        ->and(pixel($evenOdd, 3, 3)[3])->toBe(0)
        ->and(pixel($evenOdd, 1, 1)[3])->toBe(255);
});

it('paints nothing outside the rect it is given', function (): void {
    $surface = new PaintSurface(6, 6);
    $boxes = $surface->fill([square(0, 0, 6, 6)], [0xFFFFFFFF], 2, 1, 3, 2);

    expect(pixel($surface, 1, 1)[3])->toBe(0)
        ->and(pixel($surface, 2, 1)[3])->toBe(255)
        ->and(pixel($surface, 4, 2)[3])->toBe(255)
        ->and(pixel($surface, 5, 2)[3])->toBe(0)
        ->and(pixel($surface, 2, 3)[3])->toBe(0)
        ->and($boxes)->toBe([[2, 1, 3, 2]]);
});

it('answers null for an op that covers nothing', function (): void {
    $surface = new PaintSurface(4, 4);

    expect($surface->fill([square(10, 10, 12, 12), [PAINT_ELLIPSE, 2, 2, 0, 1]], [0xFFFFFFFF, 0xFFFFFFFF], 0, 0, 4, 4))->toBe([null, null])
        ->and($surface->bytes())->toBe(str_repeat("\0", 64));
});

it('paints ops in order, each over the last', function (): void {
    $surface = new PaintSurface(2, 1);
    $surface->fill([square(0, 0, 2, 1), square(1, 0, 2, 1)], [0xFF0000FF, 0x0000FFFF], 0, 0, 2, 1);

    expect(pixel($surface, 0, 0))->toBe([255, 0, 0, 255])
        ->and(pixel($surface, 1, 0))->toBe([0, 0, 255, 255]);
});

it('fills an ellipse, a ring, a rounded box and its ring', function (): void {
    $surface = new PaintSurface(12, 12);
    $surface->fill([[PAINT_ELLIPSE, 6, 6, 5, 5]], [0xFFFFFFFF], 0, 0, 12, 12, false);
    expect(pixel($surface, 6, 6)[3])->toBe(255)->and(pixel($surface, 0, 0)[3])->toBe(0)->and(pixel($surface, 1, 6)[3])->toBe(255);

    $surface = new PaintSurface(12, 12);
    $surface->fill([[PAINT_RING, 6, 6, 4, 4, 2]], [0xFFFFFFFF], 0, 0, 12, 12, false);
    expect(pixel($surface, 6, 6)[3])->toBe(0)->and(pixel($surface, 2, 6)[3])->toBe(255)->and(pixel($surface, 6, 11)[3])->toBe(0)->and(pixel($surface, 6, 9)[3])->toBe(255);

    $surface = new PaintSurface(12, 12);
    $surface->fill([[PAINT_ROUND_RECT, 0, 0, 12, 12, [4, 0, 0, 0]]], [0xFFFFFFFF], 0, 0, 12, 12, false);
    expect(pixel($surface, 0, 0)[3])->toBe(0)->and(pixel($surface, 11, 0)[3])->toBe(255)->and(pixel($surface, 4, 4)[3])->toBe(255);

    $surface = new PaintSurface(12, 12);
    $surface->fill([[PAINT_ROUND_RING, 0, 0, 12, 12, [0, 0, 0, 0], 2]], [0xFFFFFFFF], 0, 0, 12, 12, false);
    expect(pixel($surface, 0, 0)[3])->toBe(255)->and(pixel($surface, 1, 6)[3])->toBe(255)->and(pixel($surface, 2, 6)[3])->toBe(0)->and(pixel($surface, 6, 6)[3])->toBe(0);
});

it('fades a shadow from the box out to the blur', function (): void {
    $surface = new PaintSurface(20, 15);
    $surface->fill([[PAINT_SHADOW, 6, 2, 14, 13, 0, 3]], [0x000000FF], 0, 0, 20, 15);
    $alpha = fn (int $x): int => pixel($surface, $x, 7)[3];

    // deep inside is whole; the edge is half; past the blur is nothing; and it falls steadily between
    expect($alpha(10))->toBe(255)
        ->and($alpha(1))->toBe(0)
        ->and($alpha(4))->toBeLessThan($alpha(5))
        ->and($alpha(5))->toBeLessThan($alpha(6))
        ->and($alpha(6))->toBeLessThan($alpha(8));
});

it('draws one-pixel lines through points, a shared pixel once', function (): void {
    $surface = new PaintSurface(5, 5);
    $surface->clear(0x000000FF, 0, 0, 5, 5);
    // an L: down the left side, then along the bottom; the corner belongs to both
    $boxes = $surface->fill([[PAINT_POLYLINE, [0.5, 0.5, 0.5, 4.5, 4.5, 4.5], false]], [0xFFFFFF80], 0, 0, 5, 5);
    $half = [128, 128, 128, 255];

    expect(pixel($surface, 0, 0))->toBe($half)
        ->and(pixel($surface, 0, 4))->toBe($half)       // blended once, not twice
        ->and(pixel($surface, 4, 4))->toBe($half)
        ->and(pixel($surface, 1, 1))->toBe([0, 0, 0, 255])
        ->and($boxes)->toBe([[0, 0, 5, 5]]);
});

it('closes a polyline back to its first point', function (): void {
    $surface = new PaintSurface(5, 5);
    $surface->fill([[PAINT_POLYLINE, [0, 0, 4, 0, 4, 4], true]], [0xFFFFFFFF], 0, 0, 5, 5);

    expect(pixel($surface, 2, 2)[3])->toBe(255)     // the diagonal back
        ->and(pixel($surface, 1, 3)[3])->toBe(0);
});
