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
