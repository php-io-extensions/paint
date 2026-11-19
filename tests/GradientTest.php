<?php

declare(strict_types=1);

/** A ramp from black to white, opaque: colour i is (i, i, i, 255). */
function greys(): string
{
    $ramp = '';
    for ($i = 0; $i < 256; $i++) {
        $ramp .= chr($i).chr($i).chr($i)."\xFF";
    }

    return $ramp;
}

it('takes each pixel centre to gradient space and reads the ramp there', function (): void {
    $surface = new PaintSurface(4, 1);
    // u = px / 4: the centres 0.5, 1.5, 2.5, 3.5 are t = 0.125, 0.375, 0.625, 0.875
    $surface->fill([square(0, 0, 4, 1)], [[PAINT_LINEAR, PAINT_PAD, greys(), 0.25, 0, 0, 0, 0, 0]], 0, 0, 4, 1);

    // floor(t * 255 + 0.5)
    expect(array_map(fn (int $x): int => pixel($surface, $x, 0)[0], [0, 1, 2, 3]))->toBe([32, 96, 159, 223]);
});

it('holds, repeats or reflects past the ramp\'s ends', function (int $spread, array $reds): void {
    $surface = new PaintSurface(4, 1);
    // u = px - 0.5: the centres are t = 0, 1, 2, 3
    $surface->fill([square(0, 0, 4, 1)], [[PAINT_LINEAR, $spread, greys(), 1, 0, 0, 0, -0.5, 0]], 0, 0, 4, 1);

    expect(array_map(fn (int $x): int => pixel($surface, $x, 0)[0], [0, 1, 2, 3]))->toBe($reds);
})->with([
    'pad' => [PAINT_PAD, [0, 255, 255, 255]],
    'repeat' => [PAINT_REPEAT, [0, 0, 0, 0]],
    'reflect' => [PAINT_REFLECT, [0, 255, 0, 255]],
]);

it('measures a radial gradient by distance from the origin', function (): void {
    $surface = new PaintSurface(5, 5);
    // the origin at the middle pixel's centre, one unit two pixels out
    $surface->fill([square(0, 0, 5, 5)], [[PAINT_RADIAL, PAINT_PAD, greys(), 0.5, 0, 0, 0.5, -1.25, -1.25]], 0, 0, 5, 5);

    expect(pixel($surface, 2, 2)[0])->toBe(0)
        ->and(pixel($surface, 4, 2)[0])->toBe(255)
        ->and(pixel($surface, 2, 0)[0])->toBe(255)
        ->and(pixel($surface, 3, 2)[0])->toBe(128);
});

it('blends a gradient by its colour\'s alpha and the coverage', function (): void {
    $surface = new PaintSurface(1, 1);
    $surface->clear(0x000000FF, 0, 0, 1, 1);
    // one colour, white at alpha 128, over half the pixel: a = (128 * 128 + 127) / 255 = 64
    $surface->fill([square(0, 0, 0.5, 1)], [[PAINT_LINEAR, PAINT_PAD, str_repeat("\xFF\xFF\xFF\x80", 256), 0, 0, 0, 0, 0, 0]], 0, 0, 1, 1);

    expect(pixel($surface, 0, 0))->toBe([64, 64, 64, 255]);
});

it('builds a ramp by walking the stops forward, each channel straight and rounded half up', function (): void {
    // black to white over 0..1: step i is i; a stop at 0.5 makes the first half run 0 -> 200, the second 200 -> 100
    $even = PaintSurface::gradient(PAINT_LINEAR, PAINT_PAD, [0, 0, 1, 0], [[0.0, 0x000000FF], [1.0, 0xFFFFFFFF]], [1, 0, 0, 1, 0, 0])[2];
    $three = PaintSurface::gradient(PAINT_LINEAR, PAINT_PAD, [0, 0, 1, 0], [[0.0, 0x000000FF], [0.5, 0xC8C8C8FF], [1.0, 0x646464FF]], [1, 0, 0, 1, 0, 0])[2];
    $red = fn (string $ramp, int $i): int => ord($ramp[4 * $i]);

    expect(strlen($even))->toBe(PAINT_RAMP_BYTES)
        ->and([$red($even, 0), $red($even, 1), $red($even, 128), $red($even, 255)])->toBe([0, 1, 128, 255])
        // step 64: x = 64/255, u = x / 0.5 = 0.50196, 200 u = 100.39 -> 100; step 96: u = 0.75294, 200 u = 150.59 -> 151
        // (rounded half up, not down); step 191: u = (191/255 - 0.5) / 0.5 = 0.49804, 200 - 100 u = 150.2 -> 150
        ->and([$red($three, 64), $red($three, 96), $red($three, 191), $red($three, 255)])->toBe([100, 151, 150, 100]);
});

it('holds the first stop\'s colour before it and the last\'s from it on', function (): void {
    $ramp = PaintSurface::gradient(PAINT_LINEAR, PAINT_PAD, [0, 0, 1, 0], [[0.25, 0x102030FF], [0.75, 0x405060FF]], [1, 0, 0, 1, 0, 0])[2];

    expect(substr($ramp, 0, 4))->toBe("\x10\x20\x30\xFF")
        ->and(substr($ramp, 4 * 255, 4))->toBe("\x40\x50\x60\xFF");
});

it('takes pixel centres to a linear gradient\'s space through the transform undone', function (): void {
    // drawn through x' = 2x + 5, y' = 2y; the line (0, 0) -> (10, 0): u = (X - 5) / 20, v = Y / 20
    $g = PaintSurface::gradient(PAINT_LINEAR, PAINT_REFLECT, [0, 0, 10, 0], [[0.0, 0xFF]], [2, 0, 0, 2, 5, 0]);

    expect([$g[0], $g[1]])->toBe([PAINT_LINEAR, PAINT_REFLECT])
        ->and(array_map(fn (float $n): float => $n + 0.0, array_slice($g, 3)))->toBe([0.05, 0.0, 0.0, 0.05, -0.25, 0.0]);
});

it('takes pixel centres to a radial gradient\'s space: from the centre, over the radius', function (): void {
    // drawn through a turn of a quarter (x' = -y, y' = x) then (+10, 0); centre (2, 0), radius 4
    $g = PaintSurface::gradient(PAINT_RADIAL, PAINT_PAD, [2, 0, 4], [[0.0, 0xFF]], [0, 1, -1, 0, 10, 0]);
    [$a, $b, $c, $d, $e, $f] = array_slice($g, 3);
    // the pixel (10, 2) is the shape's (2, 0), the centre: u = v = 0; the pixel (10, 6) is the shape's (6, 0): u = 1
    $at = fn (float $x, float $y): array => [$a * $x + $c * $y + $e, $b * $x + $d * $y + $f];

    expect($at(10, 2))->toBe([0.0, 0.0])
        ->and($at(10, 6))->toBe([1.0, 0.0]);
});

it('is null when the transform or the geometry cannot be undone', function (array $args): void {
    expect(PaintSurface::gradient(...$args))->toBeNull();
})->with([
    'a transform that flattens' => [[PAINT_LINEAR, PAINT_PAD, [0, 0, 1, 0], [[0.0, 0xFF]], [1, 2, 2, 4, 0, 0]]],
    'a line of no length' => [[PAINT_LINEAR, PAINT_PAD, [3, 3, 3, 3], [[0.0, 0xFF]], [1, 0, 0, 1, 0, 0]]],
    'a radius of 0' => [[PAINT_RADIAL, PAINT_PAD, [0, 0, 0], [[0.0, 0xFF]], [1, 0, 0, 1, 0, 0]]],
]);

it('refuses a gradient it cannot read', function (array $args): void {
    expect(fn () => PaintSurface::gradient(...$args))->toThrow(ValueError::class);
})->with([
    'a kind it has not' => [[9, PAINT_PAD, [0, 0, 1, 0], [[0.0, 0xFF]], [1, 0, 0, 1, 0, 0]]],
    'a spread it has not' => [[PAINT_LINEAR, 7, [0, 0, 1, 0], [[0.0, 0xFF]], [1, 0, 0, 1, 0, 0]]],
    'three numbers for a line' => [[PAINT_LINEAR, PAINT_PAD, [0, 0, 1], [[0.0, 0xFF]], [1, 0, 0, 1, 0, 0]]],
    'no stops' => [[PAINT_LINEAR, PAINT_PAD, [0, 0, 1, 0], [], [1, 0, 0, 1, 0, 0]]],
    'offsets that fall' => [[PAINT_LINEAR, PAINT_PAD, [0, 0, 1, 0], [[0.6, 0xFF], [0.4, 0xFF]], [1, 0, 0, 1, 0, 0]]],
    'an offset past 1' => [[PAINT_LINEAR, PAINT_PAD, [0, 0, 1, 0], [[1.5, 0xFF]], [1, 0, 0, 1, 0, 0]]],
    'five numbers for a transform' => [[PAINT_LINEAR, PAINT_PAD, [0, 0, 1, 0], [[0.0, 0xFF]], [1, 0, 0, 1, 0]]],
]);

it('reads each pixel\'s place on its own, so a span split anywhere paints the same bytes', function (): void {
    // a repeating gradient over a 512-pixel span: worked from the span's start a step at a time, the sums drift and
    // pixel 282 lands on the next ramp entry; worked on its own, each pixel matches the same pixel painted alone
    $paint = PaintSurface::gradient(PAINT_LINEAR, PAINT_REPEAT, [1, 0, 1 + 2815 / 991, 0], [[0.0, 0x000000FF], [1.0, 0xFFFFFFFF]], [1, 0, 0, 1, 0, 0]);
    $whole = new PaintSurface(512, 1);
    $whole->fill([square(0, 0, 512, 1)], [$paint], 0, 0, 512, 1);
    $split = new PaintSurface(512, 1);
    for ($x = 0; $x < 512; $x++) {
        $split->fill([square($x, 0, $x + 1, 1)], [$paint], 0, 0, 512, 1);
    }

    expect($split->bytes())->toBe($whole->bytes());
});
